# Фильтры блочных устройств

## Введение
Фильтр блочного устройства — это модуль ядра, который перехватывает запросы к блочному устройству и выполняет предварительную обработку этих запросов. Фильтры блочных устройств позволяют расширить возможности блочного уровня. Модуль blksnap является первым фильтр-модулем, который предлагается для восходящего потока ядра Linux.

Идея перехвата запросов к блочным устройствам не новая. Ещё в ядре 2.6 существовала возможность перехвата запросов с помощью подмены функции make_request_fn, которая пренадлежала структуре request_queue. Существует несколько модулей, которые использовали эту возможность. Но ни один из них не был в дереве ядра. Потому в процессе рефакторинга эта возможность была устранена.

Поддержка ядром фильтров блочных устройств позволит вернуть возможность перехвата запросов, а также позволит делать это более безопасно. Более того, предуcмотрена возможность одновременной работы нескольких фильтров по количеству доступных альтитуд. Количество доступных альтитуд ограничено количеством фильтров в дереве ядра. Это ограничение должно стимулировать предоставление новых фильтров блочных устройств в ядро.

## Как это работает
Фильтр блочного устройства добавляется в верхней части блочного слоя.
```
 +-------------+ +-------------+
 | File System | |  Direct I/O |
 +-------------+ +-------------+
       ||              ||
       \/              \/
 +-----------------------------+
 |   | Block Device Filter|    |
 |   +--------------------+    |
 |         Block Layer         |
 +-----------------------------+
        ||  ||  ...  ||
        \/  \/       \/
       +------+   +------+
       | Disk |   | Disk |
       +------+   +------+
```
Направляемые блочному слою запросы перехватываются фильтрами и обрабатываются.

Фильтр может пропустить запрос на дальнейшее выполнение, завершить обработку запроса или перенаправить запрос на другое устройство.

В некоторых случаях фильтр не может сразу обработать запрос, в этом случае он требует повторить обработку.

Подробнее о цикле обработки фильтров в разделе "Алгоритм фильтрации".

Теоретически для кажого блочного устройства может быть добавлено одновременно несколько фильтров. Фильтры могут быть совместимы друг с другом. В этом случае они могут разместиться на своих альтитудах и обрабатывать запросы поочерёдно. Но фильтры могут быть несовместимы из-за своего назначения. В этом случае они должны использовать одну альтитуду. Это защитит систему от неработоспособной комбинации фильтров. На текущий момент зарезервирована только одна альтитуда для модуля blksnap.

## Алгоритм фильтрации
В системе фильтр представляет собой структуру со счётчиком ссылок и набором функций обратного вызова. В структуру block_device добавляется массив указателей на фильтры по количеству зарезервированных альтитуд. Массив защищается от одновременного доступа спин-блокировкой.

В функции submit_bio_noacct() добавлен вызов функции обработки фильтров filter_bio(), в которой реализован алгоритм фильтрации.

Для блочного устройства проверяются все альтитуды. Если на альтитуде был фильтр (в ячейке не NULL), то вызывается соответствующая функция обратного вызова. В зависимости от результата выполнения функции обратного вызова выполняется:
 - переход к следующей альтитуде (pass)
 - завершение обработки запроса (skip)
 - повторная обработка фильтров с первой альтитуды (redirect)
 - повторная обработка запроса (repeat)

Для того чтобы исключить рекурсивный вызов функции submit_bio(), перед вызовом функции обратного вызова инициализируется указатель на список блоков ввода/вывода current->bio_list. Если при обработке запроса инициируются запросы ввода/вывода, то они добавляются в этот список. Этот механизм позволяет защитить стек от переполнения.

После обработки запроса ввода/вывода фильтром новые запросы извлекаются из current->bio_list и выполняются. Поэтому синхронное выполнение запросов ввода/вывода в фильтре невозможно.

Тем не менее, если требуется дождаться завершения выполнения новых запросов от фильтра, то обратный вызов завершается с кодом repeat. В этом случае после обработки запросов из current->bio_list обработчик фильтра снова вызовет функцию обратного вызова фильтра, чтобы фильтр мог спокойно "вздремнуть" в ожидании выполнения ввода/вывода.

Если фильтр переводит обработку запроса ввода/вывода на другое блочное устройство, изменяя указатель на блочное устройство в bio, то обработку фильтров нужно повторить с начала, но уже для другого блочного устройства. В этом случае функция обратного вызова фильтра должна завершаться с кодом redirect.

Если фильтр решает, что оригинальный запрос ввода/вывода выполнять не требуется, то код возврата функции обратного вызова должен быть skip.

Чтобы новый запрос ввода/вывода от фильтра не попадал на обработку фильтрами, запрос может быть помечен флагом BIO_FILTERED. Такие запросы сразу пропускаются обработчиком фильтров.

## Алгоритм освобождения ресурсов фильтра
Блочное устройство может быть извлечено. В этом случае структура block_device освобождается. Фильтр в этом случае тоже должен быть освобождён. Чтобы корректно освободить фильтр, он имеет счётчик ссылок и обратный вызов для освобождения его ресурсов. Если в момент удаления блочного устройства и отключения фильтра он выполняет обработку запроса, то благодаря счётчику ссылок освобождение выполниться только тогда, когда счётчик уменьшится до нуля.

## Как этим пользоваться
Чтобы подключить свой фильтр, модуль должен инициализировать структуру bdev_filter с помощью функции bdev_filter_init() и вызвать функцию bdev_filter_attach(). Хорошей практикой будет заморозить файловую систему на блочном устройстве с помощью freeze_bdev(), но это не обязательно. Благодаря спин-блокировке новый фильтр будет добавлен безопасно.

Сразу после подключения при обработке запросов ввода/вывода будет вызываться обратный вызов submit_bio_cb() из структуры bdev_filter_operations.

Отключение фильтра может быть инициировано вызовом функции bdev_filter_detach() или автоматически при удалении блочного устройства. При этом фильтр будет удалён из структуры block_device, и будет вызвана функция обратного вызова detach_cb(). При выполнении detach_cb() нельзя переводить процесс в состояние ожидания. Если фильтру требуется дождаться завершения выполнения каких-либо процессов, рекомендуется запланировать выполнение рабочего процесса queue_work(). Важно помнить, что после завершения выполнения функции bdev_filter_detach() фильтр уже не будет получать запросы ввода/вывода на обработку.

Модулю ядра не нужно хранить указатель на структуру bdev_filter. Она уже хранится в структуре block_device. Чтобы получить фильтр, достаточно вызвать bdev_filter_get_by_altitude(). При этом счётчик ссылок будет увеличен, чтобы безопасно использовать структуру. После использования счётчик ссылок необходимо уменьшить с помощью bdev_filter_put().

## Что дальше
В текущей реализации перехватываются только вызовы submit_bio_noacct() и bdev_free_inode(). В будущем хотелось бы добавить перехват для функций bdev_read_page() и bdev_write_page(). Это позволит корректно работать с дисками, которые имеют функцию обратного вызова rw_page() в структуре block_device_operations.

В будущем количество альтитуд будет увеличиваться. Когда это количество доберётся до 8, простой массив указателей на фильтры можно будет заменить на более сложную структуру, например на xarray.