# Building and flashing commands

```
west build -b esp32c3_supermini/esp32c3
west flash
```


Alternatively:
```
west flash && minicom -D /dev/ttyACM0 -b 115200
```
