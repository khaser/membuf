Учебный модуль ядра, создающий устройства /dev/membuf*.
Данные устройства реализуют чтение и запись в хранимый ядром буфер.

Размер каждого буфера регулируется при помощи файлов /sys/class/membuf/membuf*/size,
а количество устройств регулируется при помощи /sys/class/dev_cnt.
Размер буфера при создании регулируется параметром ядра default_size.
