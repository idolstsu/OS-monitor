## Команды для работы с модулем

sudo lsmod | grep tsulab // для просмотра модуля

sudo insmod tsulab.ko // загрузка модуля

sudo rmmod tsulab // выгрузка модуля

modinfo tsulab.ko // информация о модуле

sudo journalctl --since "1 hour ago" | grep kernel // просмотр логов ядра

cat /proc/tsu // чтение из /proc файла

sudo dmesg | tail -10 // просмотр логов ядра (альтернатива)

make clean // очистка собранных файлов

make // сборка модуля
