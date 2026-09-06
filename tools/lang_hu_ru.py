# -*- coding: utf-8 -*-
"""Traducciones hu / ru del manual. Importado por make-manual.py."""

T = {}

T["hu"] = {
    "_title": "Rox-Filer2 kézikönyv",
    "_sub": "Gyors és könnyű fájlkezelő X11 és Wayland rendszerhez, a ROX-Filer folytatása.",
    "_toc": "Tartalom",
    "_footer": ("A Rox-Filer2 szabad szoftver, amelyet a GNU General Public "
                "License 2. vagy későbbi változata alatt terjesztünk. Az eredeti "
                "ROX-Filer &copy; 2005 Thomas Leonard és közreműködők; a "
                "Rox-Filer2 folytatás &copy; 2026 josejp2424."),
    "intro": ("Mi a Rox-Filer2", [
        "<p>A Rox-Filer2 a ROX-Filer folytatása: GTK3-ra frissítve, modern "
        "felülettel, natív SMB-támogatással, meghajtóikonokkal és olyan asztali "
        "móddal kiegészítve, amely X11 és Wayland alatt egyaránt működik.</p>",
        "<p>Egyetlen dolgot végez egyetlen folyamatban: fájlokat kezel, és ha "
        "kéri, az asztalt is. Nincs háttérszolgáltatás, nincs virtuális "
        "fájlrendszerréteg, és nincs függés semmilyen asztali környezettől. "
        "Minden, amit lát, közönséges POSIX útvonal.</p>",
    ]),
    "interfaces": ("Klasszikus és Modern felület", [
        "<p>A Rox-Filer2 két felületstílust kínál, amelyek ugyanazokat a fájl-, "
        "MIME-, csatolási, Kuka- és asztali motorokat használják.</p>",
        "<ul>"
        "<li><b>Klasszikus ROX</b> megtartja a ROX-Filer hagyományos "
        "elrendezését és eszköztárát. Ha a ROX-Filer felől érkezik, semmi nem "
        "került máshová.</li>"
        "<li><b>Modern</b> menüsorral, navigációs sorral, lapokkal és egy "
        "oldalsávval bővít, amelyben Helyek, Eszközök és Hálózat található.</li>"
        "</ul>",
        "<p>Az állandó stílust a <b>Beállítások &gt; Felület</b> alatt választja "
        "ki. A változás azokra az ablakokra vonatkozik, amelyeket a Rox-Filer2 "
        "újraindítása után nyit meg.</p>",
        "<p>Ha egyszeri alkalommal szeretne kipróbálni egy stílust a mentett "
        "beállítás módosítása nélkül, indítsa a <code>rox --classic</code> vagy a "
        "<code>rox --modern</code> paranccsal.</p>",
    ]),
    "window": ("A fájlkezelő ablak", [
        "<p>A könyvtárakat dupla kattintással nyithatja meg. Az egyszeri "
        "kattintás kijelöl; üres területen a bal gombbal húzva gumikeretes "
        "kijelölést kap.</p>",
        "<ul>"
        "<li>A jobb gomb a mutató alatti elem helyi menüjét nyitja meg, üres "
        "területen pedig az egész ablakét.</li>"
        "<li>A középső gomb új ablakban nyit meg egy könyvtárat.</li>"
        "<li>A <kbd>Backspace</kbd> egy szinttel feljebb lép; a <kbd>Ctrl+L</kbd> "
        "az útvonalmezőre ugrik.</li>"
        "<li>Modern felületen a <kbd>Ctrl+T</kbd> új lapot nyit, a "
        "<kbd>Ctrl+W</kbd> bezárja. Minden lap saját útvonalat és előzményt "
        "tart.</li>"
        "</ul>",
        "<p>Az ikonnézet és a részletes nézet között a nézetváltóval válthat, az "
        "ikonméretet pedig a csúszkával állítja.</p>",
    ]),
    "desktop": ("ROX asztal", [
        "<p>Az asztalt a <code>rox --desktop</code> paranccsal indítja. "
        "Egyszerre csak egy példány fut. Kirajzolja a háttérképet, megjeleníti az "
        "asztali könyvtár ikonjait, és megmutathatja a meghajtóikonokat is.</p>",
        "<ul>"
        "<li><b>Háttérkép:</b> <code>rox --desktop-wallpaper</code>, vagy jobb "
        "kattintás az asztalon. A módok: kitöltés, illesztés, nyújtás, középre "
        "és mozaik.</li>"
        "<li><b>Alkalmazások:</b> a <code>rox --desktop-apps</code> kezeli az "
        "asztali indítóikonokat.</li>"
        "<li><b>Meghajtóikonok:</b> a "
        "<code>rox --desktop-drive-icon-layout</code> rendezi őket.</li>"
        "<li><b>Beállítások:</b> <code>rox --desktop-preferences</code>.</li>"
        "</ul>",
        "<p>X11 alatt az asztal egy közönséges, asztali típusjelzésű ablakot "
        "használ. Wayland alatt a Layer Shell háttérrétegét, amihez telepített "
        "<code>gtk-layer-shell</code> és olyan kompozitor kell, amely közzéteszi "
        "a <code>zwlr_layer_shell_v1</code> protokollt, például a Labwc. A "
        "háttérprogramot a <code>rox-x11 --desktop</code> vagy a "
        "<code>rox-wayland --desktop</code> paranccsal kényszerítheti.</p>",
        "<p>Ha az X11 gyökérablak hátterét is be szeretné állítani, hogy az "
        "álátlátszó terminálok és a conky ugyanazt a képet lássák, telepítse a "
        "<code>feh</code> vagy az <code>xwallpaper</code> programot.</p>",
    ]),
    "drives": ("Meghajtók és csatolás", [
        "<p>A helyi és cserélhető meghajtók a Modern oldalsávban az "
        "<b>Eszközök</b> alatt jelennek meg, és igény szerint asztali ikonként "
        "is. Kattintson egy meghajtóra a csatoláshoz és megnyitáshoz; a "
        "leválasztáshoz használja a helyi menüt.</p>",
        "<p>A meghajtóadatok az <code>lsblk</code> programból, a "
        "<code>/proc/mounts</code> állományból és a sysfs-ből származnak, így "
        "nincs szükség háttérszolgáltatásra. Egy meghajtó csatolásához "
        "hétköznapi felhasználóként <code>udisksctl</code> vagy ezt engedélyező "
        "fstab-bejegyzés szükséges.</p>",
        "<p>A cserélhető adathordozókat mindig válassza le, mielőtt kihúzza őket. "
        "A Rox-Filer2 az ablak bezárása után nem ír az eszközre, de a kernel még "
        "tarthat vissza ki nem írt adatot.</p>",
    ]),
    "images": ("Lemezképek (ISO, IMG, SFS, SquashFS)", [
        '<p>Kattintson jobb gombbal egy lemezképfájlra, és válassza a <b>Lemezkép csatolása</b> lehetőséget. A Rox-Filer2 hurokeszközhöz rendeli, írásvédetten csatolja, majd az eredményt úgy nyitja meg, mint bármely más könyvtárat. A támogatott kiterjesztések: <code>.iso</code>, <code>.img</code>, <code>.sfs</code>, <code>.squashfs</code>, <code>.sqfs</code> és <code>.sqsh</code>.</p>',
        '<p>Csatolás után ugyanaz a menü kínálja a <b>Csatolt lemezkép megnyitása</b> és a <b>Lemezkép leválasztása</b> elemeket. A csatolások a <code>/media/rox-filer2-images/</code> alatt vannak, lemezképenként egy könyvtár.</p>',
        '<ul><li><b>Mindig írásvédett.</b> A hurokeszköz, a blokkeszköz és a csatolás is írásvédett, így a lemezkép véletlenül sem módosulhat.</li><li><b>Rendszergazdaként</b> (a Puppy és az Essora szokásos modellje) közvetlenül a <code>losetup</code> és a <code>mount</code> parancsot használja. Hétköznapi felhasználóként az <code>udisksctl</code> parancsot, amelynek telepítve kell lennie.</li><li><b>A nyers <code>.img</code> fájlok</b> tartalmazhatnak partíciós táblát. Ha egynél többet talál, a Rox-Filer2 rákérdez, melyiket csatolja, és megmutatja minden partíció valódi számát, fájlrendszerét, méretét és címkéjét. Az ISO és SquashFS lemezképek mindig egészben csatolódnak, akkor is, ha egy hibrid ISO partíciós táblát mutat.</li><li><b>Nincs GVfs.</b> A lemezkép közönséges helyi csatolássá válik, így a másolás, a fogd és vidd, a MIME-kezelés és az egyéni műveletek ugyanúgy működnek, mint bármely más könyvtárban.</li></ul>',
        '<p>A csatolás a háttérben zajlik: egy kis ablak forgó jelzővel jelenik meg, amíg a hurokeszköz elkészül és a partíciók vizsgálata tart, a fájlkezelő pedig válaszképes marad.</p>',
        '<p>Válassza le a lemezképet, mielőtt törli vagy áthelyezi a fájlt. Ha egy csatolás a Rox-Filer2 tudta nélkül szűnik meg, a menüpontok ismét a <b>Lemezkép csatolása</b> lehetőséget kínálják.</p>',
    ]),

    "network": ("SMB és Windows-megosztások", [
        "<p>Nyissa meg az <b>Ugrás &gt; Kapcsolódás SMB-megosztáshoz</b> "
        "menüpontot, vagy a <b>Hálózat böngészése</b> elemet a Modern "
        "oldalsávban. Adja meg a kiszolgálót és – ha tudja – a megosztást; a "
        "listázó gomb megkérdezi a kiszolgálót, milyen megosztásokat kínál.</p>",
        "<p>A Rox-Filer2 a megosztást a <code>mount.cifs</code> segítségével "
        "közönséges helyi csatolásként csatolja, nem pedig virtuális "
        "fájlrendszeren keresztül mutatja. Ezért a másolás, a fogd és vidd, a "
        "MIME-kezelés, a Kuka és az egyéni műveletek pontosan úgy működnek, mint "
        "a helyi fájlokon.</p>",
        "<ul>"
        "<li>A <code>cifs-utils</code> csomagnak telepítve kell lennie.</li>"
        "<li>A csatoláshoz rendszergazdai jog kell. Hétköznapi felhasználóként a "
        "Rox-Filer2 a <code>sudo -n</code> parancsot próbálja; a "
        "<code>mount.cifs</code> jelszó nélküli engedélyezése gyakorlatilag "
        "rendszergazdai jogot ad, ezért tudatosan adja meg, vagy csatoljon "
        "kézzel.</li>"
        "<li>A jelszava soha nem kerül a beállításokba. Egy ideiglenes, csak Ön "
        "által olvasható hitelesítőfájlon keresztül jut át, amelyet a program "
        "azonnal töröl, amint a <code>mount.cifs</code> visszatér. Csak a "
        "kiszolgálót, a megosztást, a felhasználót és a tartományt jegyzi meg.</li>"
        "</ul>",
    ]),
    "trash": ("Kuka", [
        "<p>A menüből való törlés az XDG Kukába helyezi az elemeket, így azok "
        "visszaállíthatók. A visszaállítás a Kuka könyvtárának helyi menüjéből "
        "érhető el.</p>",
        "<p>A Kuka csak a kukakönyvtárral azonos fájlrendszeren működik. Egy "
        "másik partíción vagy cserélhető eszközön lévő fájl törlése "
        "visszafordíthatatlan lehet, és a Rox-Filer2 ilyenkor figyelmezteti.</p>",
    ]),
    "mime": ("Fájltípusok és a Megnyitás ezzel", [
        "<p>A fájltípusok a közös MIME-adatbázisból származnak, kiegészítve a "
        "fájlra beállított kiterjesztett attribútumokkal. A helyi menü a "
        "<b>Megnyitás ezzel</b> alatt sorolja fel az adott típushoz bejegyzett "
        "alkalmazásokat.</p>",
        "<p>Állandó kezelőt vagy saját ikont az elem <b>Ikon beállítása</b> és "
        "<b>Művelet beállítása</b> menüpontjaival adhat meg. Ezek a saját "
        "beállításaiba kerülnek, és soha nem módosítják a rendszer "
        "adatbázisát.</p>",
    ]),
    "actions": ("Egyéni műveletek és sablonok", [
        "<p>Az egyéni műveletek a saját parancsait teszik a helyi menübe, a "
        "kijelölt útvonalakat argumentumként átadva. Ezek közönséges "
        "asztalbejegyzések, így egyet gépek között is átmásolhat.</p>",
        "<p>A sablonokkal a helyi menüből hozhat létre új, üres fájlt egy adott "
        "típusból. Sajátot úgy vesz fel, hogy elhelyez egy fájlt az "
        "alkalmazáskönyvtár Templates mappájában.</p>",
    ]),
    "tools": ("Keresés, könyvjelzők és páros ablakok", [
        "<ul>"
        "<li><b>Keresés:</b> a <code>rox-find</code> név, méret, típus és "
        "tartalom szerint keres, és az eredményt visszaadja a fájlkezelőnek.</li>"
        "<li><b>Könyvjelzők:</b> egy kattintásnyira tartják a gyakran használt "
        "könyvtárakat; Modern felületen a Helyek alatt is megjelennek.</li>"
        "<li><b>Páros ablakok:</b> a <code>rox --pair</code> két ablakot nyit "
        "egymás mellé a könyvtárak közötti másoláshoz, a "
        "<code>rox --pair-realign</code> pedig újraigazítja őket.</li>"
        "</ul>",
    ]),
    "options": ("Beállítások és konfigurációs fájlok", [
        "<p>A <b>Beállítások</b> a Szerkesztés menüből, az asztal menüjéből vagy "
        "a <code>rox --config-rox</code> paranccsal nyitható meg.</p>",
        "<p>A beállításai az XDG konfigurációs könyvtárban élnek, általában a "
        "<code>~/.config/rox.sourceforge.net/ROX-Filer/</code> helyen. A fájlok "
        "XML, INI és egyszerű szöveg formátumúak, így menthetők, szerkeszthetők "
        "és gépek között másolhatók. Egy fájl törlése visszaállítja az adott "
        "beállításcsoport alapértelmezéseit.</p>",
    ]),
    "cmdline": ("Parancssor", [
        "<ul>"
        "<li><code>rox</code> &mdash; automatikusan választ X11 vagy Wayland "
        "háttérprogramot.</li>"
        "<li><code>rox-x11</code>, <code>rox-wayland</code> &mdash; adott "
        "háttérprogramot kényszerít.</li>"
        "<li><code>rox KÖNYVTÁR</code> &mdash; megnyit egy könyvtárat.</li>"
        "<li><code>rox -d KÖNYVTÁR</code> &mdash; könyvtárként nyitja meg akkor "
        "is, ha alkalmazáskönyvtár.</li>"
        "<li><code>rox -D KÖNYVTÁR</code> &mdash; bezárja azt a könyvtárat és "
        "alkönyvtárait.</li>"
        "<li><code>rox --classic</code>, <code>rox --modern</code> &mdash; "
        "ideiglenes felületválasztás.</li>"
        "<li><code>rox --desktop</code> &mdash; elindítja az asztalt; a "
        "<code>--desktop-refresh</code> frissíti a futót.</li>"
        "<li><code>rox --help</code> &mdash; a teljes lista.</li>"
        "</ul>",
    ]),
    "diagnostics": ("Hibakeresés", [
        "<p>A naplózás alapértelmezetten ki van kapcsolva. Indítsa a "
        "<code>rox --debug</code> paranccsal technikai napló írásához, a "
        "<code>--log-file=FÁJL</code> kapcsolóval adja meg a helyét, a "
        "<code>--log-level=SZINT</code> értéke pedig error, warning, info, debug "
        "vagy trace lehet. A <code>rox --clear-logs</code> eltávolítja a "
        "Rox-Filer2 által automatikusan kezelt naplókat.</p>",
        "<p>Hibajelentéshez mellékelje ezt a naplót. Rögzíti, melyik asztali "
        "háttérprogram lett kiválasztva, melyik felületstílus aktív, és miért "
        "hiúsult meg egy SMB-csatolás vagy egy meghajtó-vizsgálat.</p>",
    ]),
    "support": ("Hibák bejelentése", [
        "<p>Hibákat és javaslatokat a projekt oldalán jelenthet be: "
        "<a href=\"https://github.com/josejp2424/ROX-Filer-gtk3\">"
        "github.com/josejp2424/ROX-Filer-gtk3</a>.</p>",
        "<p>Kérjük, adja meg a használt változatot (lásd <b>Súgó &gt; A "
        "Rox-Filer2 névjegye</b>), hogy X11-et vagy Waylandet használ-e, melyik "
        "felületstílust, és a pontos lépéseket, amelyekkel a hiba előidézhető.</p>",
    ]),
}

T["ru"] = {
    "_title": "Руководство Rox-Filer2",
    "_sub": "Быстрый и лёгкий файловый менеджер для X11 и Wayland, продолжение ROX-Filer.",
    "_toc": "Содержание",
    "_footer": ("Rox-Filer2 — свободное программное обеспечение, распространяемое "
                "по Стандартной общественной лицензии GNU версии 2 или более "
                "поздней. Исходный ROX-Filer &copy; 2005 Thomas Leonard и "
                "участники; продолжение Rox-Filer2 &copy; 2026 josejp2424."),
    "intro": ("Что такое Rox-Filer2", [
        "<p>Rox-Filer2 — продолжение ROX-Filer, переведённое на GTK3 и "
        "дополненное современным интерфейсом, встроенной поддержкой SMB, "
        "значками накопителей и режимом рабочего стола, который работает и в "
        "X11, и в Wayland.</p>",
        "<p>Программа делает одно дело в одном процессе: управляет файлами и, "
        "при желании, рабочим столом. Нет фонового демона, нет слоя виртуальной "
        "файловой системы и нет зависимости от среды рабочего стола. Всё, что вы "
        "видите, — обычные пути POSIX.</p>",
    ]),
    "interfaces": ("Классический и Современный интерфейсы", [
        "<p>Rox-Filer2 предлагает два стиля интерфейса, использующих одни и те "
        "же механизмы работы с файлами, MIME, монтированием, Корзиной и рабочим "
        "столом.</p>",
        "<ul>"
        "<li><b>Классический ROX</b> сохраняет традиционную компоновку и панель "
        "инструментов ROX-Filer. Если вы переходите с ROX-Filer, ничего не "
        "сдвинулось.</li>"
        "<li><b>Современный</b> добавляет строку меню, панель навигации, вкладки "
        "и боковую панель с разделами Места, Устройства и Сеть.</li>"
        "</ul>",
        "<p>Постоянный стиль выбирается в <b>Параметры &gt; Интерфейс</b>. "
        "Изменение применяется к окнам, открытым после перезапуска "
        "Rox-Filer2.</p>",
        "<p>Чтобы опробовать стиль однократно, не меняя сохранённой настройки, "
        "запустите <code>rox --classic</code> или <code>rox --modern</code>.</p>",
    ]),
    "window": ("Окно менеджера", [
        "<p>Каталоги открываются двойным щелчком. Одиночный щелчок выделяет; "
        "перетаскивание левой кнопкой по пустому месту выделяет рамкой.</p>",
        "<ul>"
        "<li>Правая кнопка открывает контекстное меню объекта под указателем, а "
        "на пустом месте — меню всего окна.</li>"
        "<li>Средняя кнопка открывает каталог в новом окне.</li>"
        "<li><kbd>Backspace</kbd> поднимает на уровень выше; <kbd>Ctrl+L</kbd> "
        "переводит фокус в поле пути.</li>"
        "<li>В Современном <kbd>Ctrl+T</kbd> открывает вкладку, а "
        "<kbd>Ctrl+W</kbd> закрывает её. Каждая вкладка хранит собственный путь и "
        "историю.</li>"
        "</ul>",
        "<p>Переключайтесь между значками и подробным списком с помощью "
        "переключателя вида, а размер значков меняйте ползунком.</p>",
    ]),
    "desktop": ("Рабочий стол ROX", [
        "<p>Запустите рабочий стол командой <code>rox --desktop</code>. "
        "Одновременно работает только один экземпляр. Он рисует обои, показывает "
        "значки вашего каталога рабочего стола и может показывать значки "
        "накопителей.</p>",
        "<ul>"
        "<li><b>Обои:</b> <code>rox --desktop-wallpaper</code> или щелчок правой "
        "кнопкой по рабочему столу. Режимы: заполнить, вписать, растянуть, по "
        "центру и мозаикой.</li>"
        "<li><b>Приложения:</b> <code>rox --desktop-apps</code> управляет "
        "значками запуска на рабочем столе.</li>"
        "<li><b>Значки накопителей:</b> "
        "<code>rox --desktop-drive-icon-layout</code> упорядочивает их.</li>"
        "<li><b>Настройки:</b> <code>rox --desktop-preferences</code>.</li>"
        "</ul>",
        "<p>В X11 рабочий стол использует обычное окно с подсказкой типа "
        "«рабочий стол». В Wayland — фоновый слой Layer Shell, для чего нужны "
        "установленный <code>gtk-layer-shell</code> и композитор, публикующий "
        "<code>zwlr_layer_shell_v1</code>, например Labwc. Принудительно выбрать "
        "механизм можно командами <code>rox-x11 --desktop</code> или "
        "<code>rox-wayland --desktop</code>.</p>",
        "<p>Чтобы фон также устанавливался на корневом окне X11 и терминалы с "
        "псевдопрозрачностью и conky видели то же изображение, установите "
        "<code>feh</code> или <code>xwallpaper</code>.</p>",
    ]),
    "drives": ("Накопители и монтирование", [
        "<p>Локальные и съёмные накопители отображаются в боковой панели "
        "Современного интерфейса в разделе <b>Устройства</b>, а при желании — и "
        "значками на рабочем столе. Щелчок по накопителю монтирует и открывает "
        "его; отмонтировать можно через контекстное меню.</p>",
        "<p>Сведения о накопителях берутся из <code>lsblk</code>, "
        "<code>/proc/mounts</code> и sysfs, поэтому фоновая служба не нужна. Для "
        "монтирования обычным пользователем требуется <code>udisksctl</code> или "
        "разрешающая запись в fstab.</p>",
        "<p>Всегда отмонтируйте съёмный носитель перед извлечением. Rox-Filer2 не "
        "пишет на устройство после закрытия окна, но ядро может удерживать ещё "
        "не записанные данные.</p>",
    ]),
    "images": ("Образы дисков (ISO, IMG, SFS, SquashFS)", [
        '<p>Щёлкните файл образа правой кнопкой и выберите <b>Смонтировать образ</b>. Rox-Filer2 подключит его к петлевому устройству, смонтирует только для чтения и откроет результат как обычный каталог. Поддерживаются расширения <code>.iso</code>, <code>.img</code>, <code>.sfs</code>, <code>.squashfs</code>, <code>.sqfs</code> и <code>.sqsh</code>.</p>',
        '<p>После монтирования в том же меню появляются пункты <b>Открыть смонтированный образ</b> и <b>Отмонтировать образ</b>. Точки монтирования находятся в <code>/media/rox-filer2-images/</code>, по каталогу на образ.</p>',
        '<ul><li><b>Всегда только для чтения.</b> Петлевое устройство, блочное устройство и само монтирование настроены только на чтение, поэтому образ невозможно изменить по ошибке.</li><li><b>От имени root</b> (обычная модель Puppy и Essora) используются <code>losetup</code> и <code>mount</code> напрямую. Обычному пользователю нужен <code>udisksctl</code>, который должен быть установлен.</li><li><b>Необработанные файлы <code>.img</code></b> могут содержать таблицу разделов. Если найдено несколько, Rox-Filer2 спросит, какой монтировать, и покажет для каждого настоящий номер раздела, файловую систему, размер и метку. Образы ISO и SquashFS всегда монтируются целиком, даже если гибридный ISO показывает таблицу разделов.</li><li><b>Без GVfs.</b> Образ становится обычной локальной точкой монтирования, поэтому копирование, перетаскивание, обработка MIME и пользовательские действия работают как в любом другом каталоге.</li></ul>',
        '<p>Монтирование выполняется в фоне: пока настраивается петлевое устройство и сканируются разделы, показывается небольшое окно с индикатором, а файловый менеджер остаётся отзывчивым.</p>',
        '<p>Отмонтируйте образ перед удалением или перемещением файла. Если монтирование исчезнет помимо Rox-Filer2, пункты меню снова предложат <b>Смонтировать образ</b>.</p>',
    ]),

    "network": ("SMB и общие ресурсы Windows", [
        "<p>Откройте <b>Переход &gt; Подключиться к ресурсу SMB</b> или "
        "<b>Обзор сети</b> в боковой панели Современного интерфейса. Укажите "
        "сервер и, если знаете, ресурс; кнопка списка спрашивает у сервера, "
        "какие ресурсы он предлагает.</p>",
        "<p>Rox-Filer2 монтирует ресурс как обычную локальную точку монтирования "
        "через <code>mount.cifs</code>, а не показывает его через виртуальную "
        "файловую систему. Поэтому копирование, перетаскивание, обработка MIME, "
        "Корзина и пользовательские действия работают точно так же, как с "
        "локальными файлами.</p>",
        "<ul>"
        "<li>Должен быть установлен <code>cifs-utils</code>.</li>"
        "<li>Монтирование требует прав root. Обычному пользователю Rox-Filer2"
        " пробует <code>sudo -n</code>; правило без пароля для "
        "<code>mount.cifs</code> фактически равносильно выдаче root, поэтому "
        "давайте его осознанно либо монтируйте вручную.</li>"
        "<li>Ваш пароль никогда не записывается в настройки. Он передаётся через "
        "временный файл учётных данных, читаемый только вами, который удаляется, "
        "как только <code>mount.cifs</code> завершает работу. Запоминаются лишь "
        "сервер, ресурс, пользователь и домен.</li>"
        "</ul>",
    ]),
    "trash": ("Корзина", [
        "<p>Удаление через меню перемещает объекты в Корзину XDG, поэтому их "
        "можно восстановить. Восстановление доступно в контекстном меню каталога "
        "Корзины.</p>",
        "<p>Корзина работает только в пределах той же файловой системы, где "
        "находится каталог корзины. Удаление файла на другом разделе или на "
        "съёмном устройстве может оказаться необратимым, и Rox-Filer2 "
        "предупредит об этом.</p>",
    ]),
    "mime": ("Типы файлов и «Открыть с помощью»", [
        "<p>Типы файлов берутся из общей базы MIME, а также из расширенных "
        "атрибутов, заданных у самого файла. Контекстное меню перечисляет "
        "зарегистрированные для этого типа приложения в разделе <b>Открыть с "
        "помощью</b>.</p>",
        "<p>Постоянный обработчик или собственный значок задаются пунктами "
        "<b>Задать значок</b> и <b>Задать действие</b> объекта. Они сохраняются "
        "в вашей собственной конфигурации и никогда не меняют системную базу.</p>",
    ]),
    "actions": ("Пользовательские действия и шаблоны", [
        "<p>Пользовательские действия добавляют ваши команды в контекстное меню, "
        "получая выделенные пути в качестве аргументов. Это обычные файлы "
        "desktop, поэтому такое действие можно перенести на другую машину.</p>",
        "<p>Шаблоны позволяют создать из контекстного меню новый пустой файл "
        "нужного типа. Свой шаблон добавляется помещением файла в каталог "
        "Templates внутри каталога приложения.</p>",
    ]),
    "tools": ("Поиск, закладки и парные окна", [
        "<ul>"
        "<li><b>Поиск:</b> <code>rox-find</code> ищет по имени, размеру, типу и "
        "содержимому и возвращает результаты менеджеру.</li>"
        "<li><b>Закладки:</b> держат часто используемые каталоги в одном щелчке; "
        "в Современном интерфейсе они также видны в разделе Места.</li>"
        "<li><b>Парные окна:</b> <code>rox --pair</code> открывает два окна "
        "рядом для копирования между каталогами, а "
        "<code>rox --pair-realign</code> заново их выравнивает.</li>"
        "</ul>",
    ]),
    "options": ("Параметры и файлы конфигурации", [
        "<p>Откройте <b>Параметры</b> из меню Правка, из меню рабочего стола или "
        "командой <code>rox --config-rox</code>.</p>",
        "<p>Ваши настройки хранятся в каталоге конфигурации XDG, обычно "
        "<code>~/.config/rox.sourceforge.net/ROX-Filer/</code>. Файлы имеют "
        "формат XML, INI и простого текста, поэтому их можно копировать, "
        "редактировать и переносить между машинами. Удаление файла возвращает эту "
        "группу настроек к значениям по умолчанию.</p>",
    ]),
    "cmdline": ("Командная строка", [
        "<ul>"
        "<li><code>rox</code> &mdash; автоматически выбирает механизм X11 или "
        "Wayland.</li>"
        "<li><code>rox-x11</code>, <code>rox-wayland</code> &mdash; "
        "принудительно выбирают механизм.</li>"
        "<li><code>rox КАТАЛОГ</code> &mdash; открывает каталог.</li>"
        "<li><code>rox -d КАТАЛОГ</code> &mdash; открывает его как каталог, даже "
        "если это каталог приложения.</li>"
        "<li><code>rox -D КАТАЛОГ</code> &mdash; закрывает этот каталог и его "
        "подкаталоги.</li>"
        "<li><code>rox --classic</code>, <code>rox --modern</code> &mdash; "
        "временный выбор интерфейса.</li>"
        "<li><code>rox --desktop</code> &mdash; запускает рабочий стол; "
        "<code>--desktop-refresh</code> обновляет уже запущенный.</li>"
        "<li><code>rox --help</code> &mdash; полный список.</li>"
        "</ul>",
    ]),
    "diagnostics": ("Диагностика", [
        "<p>Журналирование по умолчанию отключено. Запустите с "
        "<code>rox --debug</code>, чтобы вести технический журнал, "
        "<code>--log-file=ФАЙЛ</code> задаёт его расположение, а "
        "<code>--log-level=УРОВЕНЬ</code> принимает error, warning, info, debug "
        "или trace. <code>rox --clear-logs</code> удаляет журналы, которыми "
        "Rox-Filer2 управляет автоматически.</p>",
        "<p>Прикладывайте этот журнал к сообщению о проблеме. В нём записано, "
        "какой механизм рабочего стола был выбран, какой стиль интерфейса "
        "активен и почему не удалось смонтировать SMB или просканировать "
        "накопители.</p>",
    ]),
    "support": ("Сообщения о проблемах", [
        "<p>Сообщайте об ошибках и предложениях на странице проекта: "
        "<a href=\"https://github.com/josejp2424/ROX-Filer-gtk3\">"
        "github.com/josejp2424/ROX-Filer-gtk3</a>.</p>",
        "<p>Укажите используемую версию (см. <b>Справка &gt; О программе "
        "Rox-Filer2</b>), X11 у вас или Wayland, какой стиль интерфейса и точные "
        "шаги, воспроизводящие проблему.</p>",
    ]),
}
