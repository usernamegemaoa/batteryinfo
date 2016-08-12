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
Or, if superuser priviledges are required:
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

```sh
$ ./batteryinfo nDc
battery:                      0
name:                         BAT1
etd:                          4.20
charge:                       85.84%
```

```sh
$ ./batteryinfo -aj
{
"batteries": [
        {
                "battery": 0,
                "name": "BAT1",
                "charge": 92.98,
                "max_charge": 85.32,
                "voltage": 12.54,
                "current": 5.72,
                "temperature": null,
                "driver": "battery",
                "model": null,
                "manufacturer": "SANYO",
                "technology": "Li-ion",
                "status": "Discharging",
                "health": null,
                "serial_number": "0000",
                "charge_type": null,
                "charge_rate": null,
                "present": true,
                "online": null,
                "charging_enabled": null,
                "etd": 5.28
        },
]
}
```
