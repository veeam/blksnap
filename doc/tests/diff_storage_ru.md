# Тест diff_duration

## Назначение
Тест предназначен для выявления искажений данных на оригинальном блочном устройстве, на котором расположено хранилище изменений (diff storage).
Запись в хранилище изменений производится непосредственно на диск, а выделяются области в виде файлов на файловой системе.
Таким образом при ошибке алгоритма записи в хранилище изменений можно повредить метаданные файловой системы или повредить данные соседних файлов.

## Методика тестирования
Проверка блоков производится с использованием того же паттерна, что и  для теста corrupt.
При ошибке записи в область DiffSt, в областях WR должны появлятся блоки с неверным полем "смещение сектора от начала блочного устройства в секторах".
Пространство блочного устройства разделяется случайным образом на два примерно одинаковых множества диапазонов.
Получаем области доступные для записи (WR) и области, отданные под хранилище изменений (DiffSt), сменяющие один другой.

	+----+--------+----+--------+-- ... --+--------+
	| WR | DiffSt | WR | DiffSt |         | DiffSt |
	+----+--------+----+--------+-- ... --+--------+

При создании снапшота области DiffSt передаются модулю для хранения в них изменений снапшота.
Производится запись в оригинальное устройство, что вызывает работу алгоритма COW, который копирует перезапиываемый данные из области WR в области DiffSt.
Проверка выполняется с помощью проверки корректности данных в образе снапшота.
Если при записи в области DiffSt произойдёт запись в области WR, то при чтении образа снапшота при чтении из зоны WR, считанные блоки не пройдут проверку по значению смещения сектора.

## Алгоритм
1. Производится заполнение всего оригинального блочного устройства паттерном.
2. Проверяется всё содержимое блочного устройства.
3. Далее действия выполняются в основном тестовом цикле.
4. Генерируется множество случайных чисел, из них формируются области WR и DiffSt.
5. Создаётся снапшот, области DiffSt передаются модулю для храниения изменений снапшота.
6. Призводится генерация и перезапись случайных диапазонов, ограниченных областями WR, на оригинальном блочном устройстве.
7. Производится проверка образа снапшота, ограниченная областью WR.
8. Выводится сообщение об ошибках, если выявляется блок с неверным паттерном, а тест завершается.
8. При успешном прохождении теста, производится освобождение снапшота, а область DiffSt перезаписывается корректными данными.
9. В случае успеха цикл проверки повторяется, пока не пройдёт выделенное для тестирования время.

Нюанс!
В модуле veeamsnap была реализована фича, позволяющая при чтении областей, выделенных для хранения изменений снапшота, читать всегда нули.
В случае blksnap эта фича не была реализована (по крайней мере пока), поэтому при полном чтении образа снапшота из областей DiffSt будут считываться скопированнные алгоритмом COW данные, то есть некий мусор. Это не страшно, если корректно работает bitlooker. А при отсутствии bitlooker-а может вырасти размер бэкапа. При stretch алгоритме - незначительно. А вот при common -это может оказаться значительно.
