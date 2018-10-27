#define _GNU_SOURCE
#define fallthrough __attribute__ ((fallthrough))
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <time.h>
#include <sys/stat.h>
#include <pwd.h>
#include <unistd.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>
#include <X11/Xutil.h>
#include <X11/Xproto.h>
#include <pulse/pulseaudio.h>
#include <pulse/simple.h>
#include <mpv/client.h>
#include <mpv/render_gl.h>
#include <GL/gl.h>
#include <lua.h>
#include <lauxlib.h>

/* ----- */

typedef enum
{
    WOP_EXIT_SUCCESS = EXIT_SUCCESS,
    WOP_EXIT_FAILURE = EXIT_FAILURE
} wop_exit_code_t;

wop_exit_code_t wop_perror(char const *statement, char const *message)
{
    char *output;
    int err;

    err = asprintf(&output, "%s failed: %s", statement, message);
    if (err == -1)
        return WOP_EXIT_FAILURE;
    perror(output);
    free(output);
    return WOP_EXIT_FAILURE;
}

/* ----- */

#define _NET_WM_STATE_REMOVE 0
#define _NET_WM_STATE_ADD 1
#define _NET_WM_STATE_TOGGLE 2
#define WOP_MAX_PROPERTY_VALUE_LEN 4096
typedef char **wop_prop_t;

typedef struct
{
    SDL_Window *window;
    SDL_GLContext renderer;
    int width, height;
} wop_sdl_context_t;

void wop_change_atom(Display *disp, Window win, int mode, char const *atom, char const *prop)
{
    Atom xa = XInternAtom(disp, atom, False);
    Atom xp = XInternAtom(disp, prop, False);
    if (xa == None || xp == None)
        return;
    XChangeProperty(disp, win, xa, XA_ATOM, 32, mode, (unsigned char *)&xp, 1);
}

void wop_change_cardinal(Display *disp, Window win, int mode, char const *atom, CARD32 prop)
{
    Atom xa = XInternAtom(disp, atom, False);
    if (xa == None)
        return;
    XChangeProperty(disp, win, xa, XA_CARDINAL, 32, mode, (unsigned char *)&prop, 1);
}

void wop_send_atom(Display *disp, Window win, int action, char const *atom, char const *prop)
{
    XEvent event;
    long mask;
    Atom xa, xp;
    int err;

    xa = XInternAtom(disp, atom, False);
    xp = XInternAtom(disp, prop, False);
    if (xa == None || xp == None) {
        wop_perror(atom, "atom name is not in the Host Portable Character Encoding");
        return;
    }
    mask = SubstructureRedirectMask | SubstructureNotifyMask;
    event.xclient.type = ClientMessage;
    event.xclient.serial = 0;
    event.xclient.send_event = True;
    event.xclient.message_type = xa;
    event.xclient.window = win;
    event.xclient.format = 32;
    event.xclient.data.l[0] = action;
    event.xclient.data.l[1] = xp;
    event.xclient.data.l[2] = 0;
    event.xclient.data.l[3] = 2;
    event.xclient.data.l[4] = 0;
    err = XSendEvent(disp, DefaultRootWindow(disp), False, mask, &event);
    if (err == 0)
        wop_perror("XSendEvent()", "conversion to wire protocol format failed");
}

void wop_send_cardinal(Display *disp, Window win, char const *atom, long val)
{
    XEvent event;
    long mask;
    Atom xa;
    int err;

    xa = XInternAtom(disp, atom, False);
    if (xa == None) {
        wop_perror(atom, "atom name is not in the Host Portable Character Encoding");
        return;
    }
    mask = SubstructureRedirectMask | SubstructureNotifyMask;
    event.xclient.type = ClientMessage;
    event.xclient.serial = 0;
    event.xclient.send_event = True;
    event.xclient.message_type = xa;
    event.xclient.window = win;
    event.xclient.format = 32;
    event.xclient.data.l[0] = val;
    event.xclient.data.l[1] = 2;
    event.xclient.data.l[2] = 0;
    event.xclient.data.l[3] = 0;
    event.xclient.data.l[4] = 0;
    err = XSendEvent(disp, DefaultRootWindow(disp), False, mask, &event);
    if (err == 0)
        wop_perror("XSendEvent()", "conversion to wire protocol format failed");
}

wop_exit_code_t wop_init_sdl(wop_sdl_context_t *ctx)
{
    if (SDL_Init(SDL_INIT_VIDEO) < 0)
        return wop_perror("SDL_Init()", SDL_GetError());
    ctx->window =
        SDL_CreateWindow("Wall of Pimp", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                         0, 0, SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN_DESKTOP);
    if (ctx->window == NULL)
        return wop_perror("SDL_CreateWindow()", SDL_GetError());
    ctx->renderer = SDL_GL_CreateContext(ctx->window);
    if (ctx->renderer == NULL)
        return wop_perror("SDL_GL_CreateContext()", SDL_GetError());
    SDL_GetWindowSize(ctx->window, &ctx->width, &ctx->height);
    {
        SDL_bool err;
        SDL_SysWMinfo info;
        Display *display;
        Window window;

        SDL_VERSION(&info.version);
        err = SDL_GetWindowWMInfo(ctx->window, &info);
        if (err == SDL_FALSE)
            return wop_perror("SDL_GetWindowWMInfo()", SDL_GetError());
        display = info.info.x11.display;
        window = info.info.x11.window;
        XUnmapWindow(display, window);
        XSync(display, window);
        XMoveWindow(display, window, 0, 0);
        XWMHints wmHint = {0};
        wmHint.flags = InputHint | StateHint;
        wmHint.input = False;
        wmHint.initial_state = NormalState;
        XSetWMProperties(display, window, NULL, NULL, NULL, 0, NULL, &wmHint, NULL);
        wop_change_atom(display, window, PropModeReplace, "_NET_WM_WINDOW_TYPE", "_NET_WM_WINDOW_TYPE_DESKTOP");
        wop_change_cardinal(display, window, PropModeReplace, "_WIN_LAYER", 0);
        wop_change_cardinal(display, window, PropModeReplace, "_NET_WM_DESKTOP", 0xFFFFFFFF);
        wop_send_cardinal(display, window, "_NET_WM_DESKTOP", 0xFFFFFFFF);
        wop_change_atom(display, window, PropModeReplace, "_NET_WM_ALLOWED_ACTIONS", "_NET_WM_ACTION_FULLSCREEN");
        wop_change_atom(display, window, PropModeAppend, "_NET_WM_STATE", "_NET_WM_STATE_FULLSCREEN");
        wop_send_atom(display, window, _NET_WM_STATE_ADD, "_NET_WM_STATE", "_NET_WM_STATE_FULLSCREEN");
        wop_change_atom(display, window, PropModeAppend, "_NET_WM_ALLOWED_ACTIONS", "_NET_WM_ACTION_BELOW");
        wop_change_atom(display, window, PropModeAppend, "_NET_WM_STATE", "_NET_WM_STATE_BELOW");
        wop_send_atom(display, window, _NET_WM_STATE_ADD, "_NET_WM_STATE", "_NET_WM_STATE_BELOW");
        wop_change_atom(display, window, PropModeAppend, "_NET_WM_ALLOWED_ACTIONS", "_NET_WM_ACTION_STICK");
        wop_change_atom(display, window, PropModeAppend, "_NET_WM_STATE", "_NET_WM_STATE_STICKY");
        wop_send_atom(display, window, _NET_WM_STATE_ADD, "_NET_WM_STATE", "_NET_WM_STATE_STICKY");
        XSync(display, window);
        XMapWindow(display, window);
        XSync(display, window);
    }
    return WOP_EXIT_SUCCESS;
}

void wop_destroy_sdl(wop_sdl_context_t *ctx)
{
    SDL_GL_DeleteContext(ctx->renderer);
    SDL_DestroyWindow(ctx->window);
    SDL_Quit();
}

/* ----- */

typedef struct
{
    pa_mainloop *mainloop;
    pa_mainloop_api *api;
    pa_context *instance;
    char *source;
} wop_pusle_async_context_t;

void wop_destroy_pulse_async(wop_pusle_async_context_t *ctx)
{
    pa_context_disconnect(ctx->instance);
    pa_context_unref(ctx->instance);
    pa_mainloop_free(ctx->mainloop);
}

void wop_source_pulse_async_cb(pa_context *self, const pa_server_info *i, void *data)
{
    (void)self;
    int err;

    wop_pusle_async_context_t *ctx = data;
    err = asprintf(&ctx->source, "%s.monitor", i->default_sink_name);
    if (err == -1)
        ctx->source = strdup(strerror(errno));
    pa_mainloop_quit(ctx->mainloop, 0);
    wop_destroy_pulse_async(ctx);
}

void wop_state_pulse_async_cb(pa_context *self, void *data)
{
    (void)self;

    wop_pusle_async_context_t *ctx = data;
    switch (pa_context_get_state(ctx->instance)) {
    case PA_CONTEXT_READY:
        pa_operation_unref(pa_context_get_server_info(ctx->instance, wop_source_pulse_async_cb, data));
        break;
    case PA_CONTEXT_FAILED:
        wop_perror("pa_context_get_state() != PA_CONTEXT_FAILED", pa_strerror(pa_context_errno(ctx->instance)));
        pa_mainloop_quit(ctx->mainloop, 0);
        wop_destroy_pulse_async(ctx);
        break;
    default: break;
    }
}

wop_exit_code_t wop_setup_pulse_async(wop_pusle_async_context_t *ctx)
{
    int err;

    ctx->mainloop = pa_mainloop_new();
    if (ctx->mainloop == NULL)
        return wop_perror("pa_mainloop_new()", "pulse mainloop");
    ctx->api = pa_mainloop_get_api(ctx->mainloop);
    ctx->instance = pa_context_new(ctx->api, "wop");
    if (ctx->instance == NULL)
        return wop_perror("pa_context_new()", "wop context");
    err = pa_context_connect(ctx->instance, NULL, PA_CONTEXT_NOFLAGS, NULL);
    if (err < 0)
        return wop_perror("pa_context_connect()", pa_strerror(err));
    pa_context_set_state_callback(ctx->instance, wop_state_pulse_async_cb, ctx);
    err = pa_mainloop_iterate(ctx->mainloop, 0, NULL);
    if (err < 0)
        return wop_perror("pa_mainloop_iterate()", pa_strerror(err));
    err = pa_mainloop_run(ctx->mainloop, NULL);
    if (err < 0)
        return wop_perror("pa_mainloop_run()", pa_strerror(err));
    return WOP_EXIT_SUCCESS;
}

typedef struct
{
    int bufflen;
    int buffsize;
    pa_simple *simple;
    int16_t *buffer;
} wop_pulse_context_t;

wop_exit_code_t wop_init_pulse(wop_pulse_context_t *ctx)
{
    wop_pusle_async_context_t pulse_async;
    int err;

    ctx->bufflen = 1024;
    const pa_sample_spec ss = {
        .format = PA_SAMPLE_S16LE,
        .rate =  44100,
        .channels = 2
    };
    const pa_buffer_attr pb = {
        .maxlength = ctx->bufflen * 2,
        .fragsize = ctx->bufflen
    };
    pulse_async.source = NULL;
    err = wop_setup_pulse_async(&pulse_async);
    if (err == WOP_EXIT_FAILURE)
        return WOP_EXIT_FAILURE;
    ctx->simple = pa_simple_new(NULL, "wop", PA_STREAM_RECORD, pulse_async.source, "audio output", &ss, NULL, &pb, &err);
    if (ctx->simple == NULL)
        return wop_perror("pa_simple_new()", pa_strerror(err));
    if (pulse_async.source != NULL)
        free(pulse_async.source);
    ctx->buffsize = ctx->bufflen / 2 * sizeof(int16_t);
    ctx->buffer = malloc(ctx->buffsize);
    if (ctx->buffer == NULL)
        return wop_perror("malloc()", "pulse buffer");
    return WOP_EXIT_SUCCESS;
}

void wop_destroy_pulse(wop_pulse_context_t *ctx)
{
    free(ctx->buffer);
    pa_simple_free(ctx->simple);
}

/* ----- */

typedef struct
{
    mpv_handle *handle;
    mpv_render_context *renderer;
    Uint32 sdl_on_mpv_redraw;
} wop_mpv_context_t;

void *wop_proc_address_mpv_cb(void *data, char const *name)
{
    (void)data;
    return SDL_GL_GetProcAddress(name);
}

void wop_redraw_mpv_cb(void *data)
{
    Uint32 *sdl_on_mpv_redraw = data;
    SDL_Event event = { .type = *sdl_on_mpv_redraw };
    SDL_PushEvent(&event);
}

wop_exit_code_t wop_init_mpv(wop_mpv_context_t *ctx, char const *filepath)
{
    int err;

    ctx->handle = mpv_create();
    if (ctx->handle == NULL)
        return wop_perror("mpv_create()", "out of memory or LC_NUMERIC not set to \"C\"");
    err = mpv_initialize(ctx->handle);
    if (err < 0)
        return wop_perror("mpv_initialize()", mpv_error_string(err));
    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_API_TYPE, MPV_RENDER_API_TYPE_OPENGL},
        {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &(mpv_opengl_init_params){
            .get_proc_address = wop_proc_address_mpv_cb,
        }},
        {0}
    };
    err = mpv_render_context_create(&ctx->renderer, ctx->handle, params);
    if (err < 0)
        return wop_perror("mpv_render_context_create()", mpv_error_string(err));
    ctx->sdl_on_mpv_redraw = SDL_RegisterEvents(1);
    if (ctx->sdl_on_mpv_redraw == (Uint32)-1)
        return wop_perror("SDL_RegisterEvents()", SDL_GetError());
    mpv_render_context_set_update_callback(ctx->renderer, wop_redraw_mpv_cb, &ctx->sdl_on_mpv_redraw);
    err = mpv_set_option_string(ctx->handle, "aid", "no");
    if (err < 0)
        return wop_perror("mpv_set_option_string()", mpv_error_string(err));
    err = mpv_set_option_string(ctx->handle, "loop", "inf");
    if (err < 0)
        return wop_perror("mpv_set_option_string()", mpv_error_string(err));
    err = mpv_command(ctx->handle, (char const *[]){"loadfile", filepath, NULL});
    if (err < 0)
        return wop_perror("mpv_command()", mpv_error_string(err));
    while (mpv_wait_event(ctx->handle, 0)->event_id != MPV_EVENT_FILE_LOADED);
    return WOP_EXIT_SUCCESS;
}

void wop_destroy_mpv(wop_mpv_context_t *ctx)
{
    mpv_render_context_free(ctx->renderer);
    mpv_terminate_destroy(ctx->handle);
}

/* ----- */

typedef struct
{
    char *path;
    time_t timestamp;
    lua_State *lua;
    int fresh;
} wop_conf_context_t;

wop_exit_code_t wop_init_conf(wop_conf_context_t *ctx)
{
    int err;

    {
        char *fmt;
        char *var = getenv("XDG_CONFIG_HOME");
        if (var == NULL) {
            var = getpwuid(getuid())->pw_dir;
            fmt = "%s/.config/wop.lua";
        } else
            fmt = "%s/wop.lua";
        err = asprintf(&ctx->path, fmt, var);
        if (err == -1) {
            return wop_perror("asprintf()", "config file path");
        }
    }
    err = access(ctx->path, F_OK);
    if (err == -1) {
        FILE *f = fopen(ctx->path, "w");
        if (f == NULL)
            return wop_perror("fopen()", ctx->path);
        fputs("gap_width = 0.5\ndensity = 3\nfps = 30\ntimer = 10\n", f);
        fclose(f);
    }
    err = access(ctx->path, R_OK);
    if (err == -1)
        return wop_perror("access()", ctx->path);
    {
        ctx->lua = luaL_newstate();
        if (ctx->lua == NULL)
            return wop_perror("luaL_newstate()", "lua state");
        int err = luaL_dofile(ctx->lua, ctx->path);
        if (err != LUA_OK) {
            wop_perror("luaL_loadfile()", lua_tostring(ctx->lua, -1));
            lua_pop(ctx->lua, 1);
            return WOP_EXIT_FAILURE;
        }
    }
    {
        struct stat result;
        err = stat(ctx->path, &result);
        if(err == -1)
            return wop_perror("stat()", ctx->path);
        ctx->timestamp = result.st_mtime;
    }
    ctx->fresh = 1;
    return WOP_EXIT_SUCCESS;
}

void wop_destroy_conf(wop_conf_context_t *ctx)
{
    lua_close(ctx->lua);
    if (ctx->path != NULL)
        free(ctx->path);
}

wop_exit_code_t wop_update_conf(wop_conf_context_t *ctx)
{
    struct stat result;
    int err;

    err = stat(ctx->path, &result);
    if(err == -1)
        return wop_perror("stat()", ctx->path);
    err = (int)difftime(ctx->timestamp, result.st_mtime);
    if (err == 0)
        return WOP_EXIT_SUCCESS;
    ctx->timestamp = result.st_mtime;
    {
        int err = luaL_dofile(ctx->lua, ctx->path);
        if (err != LUA_OK) {
            wop_perror("luaL_loadfile()", lua_tostring(ctx->lua, -1));
            lua_pop(ctx->lua, 1);
            return WOP_EXIT_FAILURE;
        }
    }
    ctx->fresh = 1;
    return WOP_EXIT_SUCCESS;
}

/* ----- */

#define WOP_FP_DIV(A, B) (((float)(A)) / ((float)(B)))
#define WOP_FP_MUL(A, B) (((float)(A)) * ((float)(B)))

#define I16_MAX 0x7fff
#define I16_MIN (-I16_MAX - 1)
#define WOP_BAR_HEIGHT(X) (X >= 0 ? WOP_FP_DIV(X, I16_MAX) : -WOP_FP_DIV(X, I16_MIN))

typedef struct
{
    float free_width;
    float bar_width;
    float gap_width;
    int density;
    float delay;
    struct { Uint32 duration, cap; } timer;
    enum {
        WOP_SINGLE, WOP_DOUBLE, WOP_TRIPLE,
        WOP_SIDES, WOP_BASELINES
    } baseline;
} wop_draw_context_t;

void wop_signal_cb(int signum)
{
    (void)signum;
    SDL_Event event = { .type = SDL_QUIT };
    SDL_PushEvent(&event);
    printf("\n");
}

wop_exit_code_t wop_usage(char const *name)
{
    printf("Usage: %s file\n", name);
    return WOP_EXIT_FAILURE;
}

void wop_load_conf(wop_draw_context_t *ctx, wop_sdl_context_t *sdl, wop_pulse_context_t *pulse, wop_conf_context_t *conf)
{
    lua_getglobal(conf->lua, "gap_width");
    lua_getglobal(conf->lua, "density");
    lua_getglobal(conf->lua, "fps");
    lua_getglobal(conf->lua, "timer");
    ctx->free_width = WOP_FP_DIV(WOP_FP_DIV(sdl->width, WOP_FP_DIV(pulse->buffsize, 4)), sdl->width);
    ctx->gap_width = WOP_FP_MUL(lua_tonumber(conf->lua, -4), ctx->free_width);
    ctx->bar_width = ctx->free_width - ctx->gap_width;
    ctx->density = lua_tointeger(conf->lua, -3);
    ctx->delay = WOP_FP_DIV(1000, lua_tointeger(conf->lua, -2));
    ctx->timer.duration = 0;
    ctx->timer.cap = lua_tointeger(conf->lua, -1) * 1000;
    lua_pop(conf->lua, 4);
    conf->fresh = 0;
}

void wop_random_color(wop_sdl_context_t *ctx)
{
    int x, y;
    struct { GLubyte r, g, b; } pixel;

    if (rand() % 2 == 0) {
        x = ctx->width / 2;
        y = rand() % ctx->height;
    } else {
        x = rand() % ctx->width;
        y = ctx->height / 2;
    }
    glReadPixels(x, y, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, &pixel);
    glColor3f(WOP_FP_DIV(pixel.r, 0xFF), WOP_FP_DIV(pixel.g, 0xFF), WOP_FP_DIV(pixel.b, 0xFF));
}

int wop_densify(int16_t *buffer, int size, int density)
{
    int lav = 0, rav = 0;
    int processed = 0;

    for (int i = 0; i < size && processed < density && buffer[i] < 0 && buffer[i + 1] < 0; i += 2) {
        lav += buffer[i];
        rav += buffer[i + 1];
        processed++;
    }
    if (processed > 0)
        goto ret;
    for (int i = 0; i < size && processed < density && buffer[i] >= 0 && buffer[i + 1] >= 0; i += 2) {
        lav += buffer[i];
        rav += buffer[i + 1];
        processed++;
    }
    if (processed == 0)
        return 1;
ret:
    buffer[0] = lav / processed;
    buffer[1] = rav / processed;
    return processed;
}

void wop_draw(wop_draw_context_t *ctx, wop_pulse_context_t *pulse)
{
    int read_err, pulse_err, step;

    read_err = pa_simple_read(pulse->simple, pulse->buffer, pulse->buffsize, &pulse_err);
    if (read_err < 0) {
        wop_perror("pa_simple_read()", pa_strerror(pulse_err));
        return;
    }
    step = 1;
    for (int i = 0; i < pulse->bufflen / 2; i += (step * 2)) {
        int16_t l = pulse->buffer[i], r = pulse->buffer[i + 1];
        step = wop_densify(pulse->buffer, pulse->bufflen / 2 - i, ctx->density);
        float gap = WOP_FP_DIV(WOP_FP_MUL(ctx->gap_width, step), 2);
        float pos = WOP_FP_MUL(i / 2, ctx->free_width) + gap;
        switch (ctx->baseline) {
        case WOP_SINGLE: case WOP_TRIPLE:
            if (l != 0)
                glRectf(-pos, WOP_BAR_HEIGHT(l), -(pos + ctx->bar_width * step), 0);
            if (r != 0)
                glRectf(pos, WOP_BAR_HEIGHT(r), pos + ctx->bar_width * step, 0);
            if (ctx->baseline != WOP_TRIPLE)
                break;
            fallthrough;
        case WOP_DOUBLE:
            if (l < 0)
                glRectf(-pos, 1, -(pos + ctx->bar_width * step), 1 + WOP_FP_MUL(0.5, WOP_BAR_HEIGHT(l)));
            else if (l > 0)
                glRectf(-pos, -1 + WOP_FP_MUL(0.5, WOP_BAR_HEIGHT(l)), -(pos + ctx->bar_width * step), -1);
            if (r < 0)
                glRectf(pos, 1, pos + ctx->bar_width * step, 1 + WOP_FP_MUL(0.5, WOP_BAR_HEIGHT(r)));
            else if (r > 0)
                glRectf(pos, -1 + WOP_FP_MUL(0.5, WOP_BAR_HEIGHT(r)), pos + ctx->bar_width * step, -1);
            break;
        case WOP_SIDES:
            if (l != 0)
                glRectf(-1, 1 - pos * 2, -1 + WOP_FP_MUL(0.5, WOP_BAR_HEIGHT(abs(l))), 1 - (pos + ctx->bar_width * step) * 2);
            if (r != 0)
                glRectf(1 - WOP_FP_MUL(0.5, WOP_BAR_HEIGHT(abs(r))), 1 - pos * 2, 1, 1 - (pos + ctx->bar_width * step) * 2);
            break;
        default: break;
        }
    }
}

int main(int ac, char **av)
{
    wop_sdl_context_t sdl;
    wop_pulse_context_t pulse;
    wop_mpv_context_t mpv;
    wop_conf_context_t conf;
    wop_draw_context_t ctx;

    if (ac != 2)
        return wop_usage(av[0]);

    if (wop_init_sdl(&sdl) == WOP_EXIT_FAILURE ||
        wop_init_pulse(&pulse) == WOP_EXIT_FAILURE ||
        wop_init_mpv(&mpv, av[1]) == WOP_EXIT_FAILURE ||
        wop_init_conf(&conf) == WOP_EXIT_FAILURE)
        return WOP_EXIT_FAILURE;

    signal(SIGINT, wop_signal_cb);
    signal(SIGTERM, wop_signal_cb);
    signal(SIGABRT, wop_signal_cb);
    srand(time(NULL));
    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_OPENGL_FBO, &(mpv_opengl_fbo){
            .fbo = 0, .w = sdl.width, .h = sdl.height
        }},
        {MPV_RENDER_PARAM_FLIP_Y, &(int){1}},
        {0}
    };

    wop_load_conf(&ctx, &sdl, &pulse, &conf);
    ctx.baseline = WOP_SINGLE;

    for (;;) {
        SDL_Event event;
        Uint32 start, duration;
        int redraw = 0;

        start = SDL_GetTicks();
        if (SDL_WaitEvent(&event) == 0) {
            wop_perror("SDL_WaitEvent()", SDL_GetError());
            goto cleanup;
        }
        switch (event.type) {
        case SDL_QUIT:
            goto cleanup;
        case SDL_WINDOWEVENT:
            if (event.window.event == SDL_WINDOWEVENT_EXPOSED)
                redraw = 1;
            break;
        default:
            if (event.type == mpv.sdl_on_mpv_redraw)
                redraw = 1;
        }
        if (redraw) {
            mpv_render_context_render(mpv.renderer, params);
            wop_random_color(&sdl);
            wop_draw(&ctx, &pulse);
            SDL_GL_SwapWindow(sdl.window);
        }
        duration = SDL_GetTicks() - start;
        if (ctx.timer.duration > ctx.timer.cap) {
            ctx.baseline = rand() % WOP_BASELINES;
            wop_update_conf(&conf);
            if (conf.fresh == 1)
                wop_load_conf(&ctx, &sdl, &pulse, &conf);
            ctx.timer.duration = 0;
            duration = SDL_GetTicks() - start;
        } else
            ctx.timer.duration += duration;
        if (duration < ctx.delay)
            SDL_Delay(ctx.delay - duration);
    }
cleanup:
    printf("Cleaning up\n");
    wop_destroy_conf(&conf);
    wop_destroy_mpv(&mpv);
    wop_destroy_pulse(&pulse);
    wop_destroy_sdl(&sdl);
    return WOP_EXIT_SUCCESS;
}
