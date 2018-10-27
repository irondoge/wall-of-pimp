Wall of Pimp
======
**Wall of pimp** is a desktop background audio visualizer allowing video backgrounds.

#### Screenshot
![Screenshot](https://github.com/irondoge/wall-of-pimp/raw/master/capture.gif "screenshot")

## Download

* [Version 1.0](https://github.com/irondoge/wall-of-pimp/releases/tag/1.0.0)

## Installation
Distribution packages are planned in future releases.

#### Prerequisities
* libSDL2
* libX11
* libpulse
* libpulse-simple
* libmpv
* libGL
* liblua

#### Build
`$ make`

#### Install
`# cp wop /usr/local/bin`

## Usage
```
Usage: wop file
```

## API Reference
A configuration file located at `$XDG_CONFIG_HOME/wop.lua` or `~/.config/wop.lua` is automatically reloaded during execution.
It supports these options:
* `gap_width`: percentage value of gap space separating bars (between 0 and 1)
* `density`: number of similar bars to stack together
* `fps`: FPS (frames per second) cap
* `timer`: time interval in seconds to reload the configuration file

## Contributors
#### Contributors on GitHub
* [Contributors](https://github.com/irondoge/wall-of-pimp/graphs/contributors)

## License
License will be available in future releases.
