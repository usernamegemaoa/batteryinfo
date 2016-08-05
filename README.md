# batteryinfo
A small battery information tool for Linux systems.

# Building and installation
To build:
```sh
$ make
```

And then to install (both the executable and the manpage):
```sh
$ make install
```
Or:
```sh
$ sudo make install
```

# Example usage

```sh
$ ./batteryinfo
battery:                      0
charge:                       29.87%
voltage:                      10.98
current:                      11.08
name:                         BAT1
model:                        ?
manufacturer:                 SANYO
technology:                   Li-ion
status:                       Discharging
present:                      yes
```
