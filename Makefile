INCFLAGS :=
STDFLAG :=	-std=c11
CFLAGS :=	$(INCFLAGS) -W -Wall -Wextra -pedantic $(STDFLAG)

LDLIBS :=	SDL2 X11 pulse pulse-simple mpv GL lua
LDLIBS :=	$(addprefix -l,$(LDLIBS))

SRCDIR :=	.
SRC :=		main.c
SRC :=		$(addprefix $(SRCDIR)/,$(SRC))
BUILDDIR :=	.
OBJ :=		$(patsubst $(SRCDIR)/%.c,$(BUILDDIR)/%.o,$(SRC))

OUTDIR :=	.
NAME :=		$(OUTDIR)/wop

all:		$(NAME)

$(NAME):	$(OBJ)
		gcc -o $@ $^ $(LDLIBS)

$(BUILDDIR)/%.o: $(SRCDIR)/%.c
		gcc -c $(CFLAGS) $< -o $@

RM :=		@$(RM) -v

mostlyclean:
		$(RM) $(OBJ) $(NAME)

re:		mostlyclean all