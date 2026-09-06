# -*- coding: utf-8 -*-
"""Traducciones it / ca / de del manual. Importado por make-manual.py."""

T = {}

T["it"] = {
    "_title": "Manuale di Rox-Filer2",
    "_sub": "Gestore di file veloce e leggero per X11 e Wayland, continuazione di ROX-Filer.",
    "_toc": "Indice",
    "_footer": ("Rox-Filer2 è software libero distribuito secondo la Licenza "
                "Pubblica Generica GNU, versione 2 o successiva. ROX-Filer "
                "originale &copy; 2005 Thomas Leonard e collaboratori; "
                "continuazione Rox-Filer2 &copy; 2026 josejp2424."),
    "intro": ("Che cos'è Rox-Filer2", [
        "<p>Rox-Filer2 è la continuazione di ROX-Filer, aggiornata a GTK3 ed "
        "estesa con un'interfaccia moderna, supporto SMB nativo, icone delle "
        "unità e una modalità scrivania che funziona sia su X11 sia su "
        "Wayland.</p>",
        "<p>Fa una cosa sola in un solo processo: gestisce i file e, se lo "
        "desidera, la scrivania. Non c'è alcun demone, né uno strato di file "
        "system virtuale, né dipendenza da un ambiente desktop. Tutto ciò che "
        "vede sono normali percorsi POSIX.</p>",
    ]),
    "interfaces": ("Interfacce Classica e Moderna", [
        "<p>Rox-Filer2 offre due stili di interfaccia che condividono gli stessi "
        "motori per file, MIME, montaggio, Cestino e Scrivania.</p>",
        "<ul>"
        "<li><b>Classica ROX</b> mantiene la disposizione e la barra degli "
        "strumenti tradizionali di ROX-Filer. Se viene da ROX-Filer, nulla si è "
        "spostato.</li>"
        "<li><b>Moderna</b> aggiunge una barra dei menu, una riga di navigazione, "
        "schede e una barra laterale con Risorse, Dispositivi e Rete.</li>"
        "</ul>",
        "<p>Scelga lo stile permanente in <b>Opzioni &gt; Interfaccia</b>. La "
        "modifica si applica alle finestre aperte dopo il riavvio di "
        "Rox-Filer2.</p>",
        "<p>Per provare uno stile una sola volta senza cambiare la preferenza "
        "salvata, avvii <code>rox --classic</code> o <code>rox --modern</code>.</p>",
    ]),
    "window": ("La finestra del gestore", [
        "<p>Apra le cartelle con un doppio clic. Un clic singolo seleziona; "
        "trascini con il pulsante sinistro su uno spazio vuoto per la selezione "
        "a rettangolo.</p>",
        "<ul>"
        "<li>Il pulsante destro apre il menu contestuale dell'elemento sotto il "
        "puntatore, o quello dell'intera finestra su spazio vuoto.</li>"
        "<li>Il pulsante centrale apre una cartella in una nuova finestra.</li>"
        "<li><kbd>Backspace</kbd> risale di un livello; <kbd>Ctrl+L</kbd> attiva "
        "la barra del percorso.</li>"
        "<li>In Moderna, <kbd>Ctrl+T</kbd> apre una scheda e <kbd>Ctrl+W</kbd> la "
        "chiude. Ogni scheda mantiene percorso e cronologia propri.</li>"
        "</ul>",
        "<p>Passi dalla vista a icone alla vista dettagliata con il selettore di "
        "vista e regoli la dimensione delle icone con il cursore.</p>",
    ]),
    "desktop": ("Scrivania ROX", [
        "<p>Avvii la scrivania con <code>rox --desktop</code>. Viene eseguita una "
        "sola istanza alla volta. Disegna lo sfondo, mostra le icone della sua "
        "cartella scrivania e può mostrare le icone delle unità.</p>",
        "<ul>"
        "<li><b>Sfondo:</b> <code>rox --desktop-wallpaper</code>, oppure clic "
        "destro sulla scrivania. Le modalità sono riempi, adatta, allunga, centra "
        "e affianca.</li>"
        "<li><b>Applicazioni:</b> <code>rox --desktop-apps</code> gestisce i "
        "lanciatori della scrivania.</li>"
        "<li><b>Icone delle unità:</b> "
        "<code>rox --desktop-drive-icon-layout</code> le dispone.</li>"
        "<li><b>Preferenze:</b> <code>rox --desktop-preferences</code>.</li>"
        "</ul>",
        "<p>Su X11 la scrivania usa una normale finestra con suggerimento di tipo "
        "desktop. Su Wayland usa il livello di sfondo di Layer Shell, che "
        "richiede <code>gtk-layer-shell</code> installato e un compositor che "
        "pubblichi <code>zwlr_layer_shell_v1</code>, come Labwc. Forzi un backend "
        "con <code>rox-x11 --desktop</code> o <code>rox-wayland --desktop</code>.</p>",
        "<p>Per impostare anche lo sfondo della finestra radice di X11, così che "
        "i terminali con falsa trasparenza e conky vedano la stessa immagine, "
        "installi <code>feh</code> o <code>xwallpaper</code>.</p>",
    ]),
    "drives": ("Unità e montaggio", [
        "<p>Le unità locali e rimovibili compaiono nella barra laterale Moderna "
        "sotto <b>Dispositivi</b> e, facoltativamente, come icone sulla "
        "scrivania. Faccia clic su un'unità per montarla e aprirla; usi il menu "
        "contestuale per smontarla.</p>",
        "<p>Le informazioni sulle unità provengono da <code>lsblk</code>, da "
        "<code>/proc/mounts</code> e da sysfs, quindi non serve alcun servizio in "
        "background. Montare un'unità come utente normale richiede "
        "<code>udisksctl</code> o una voce in fstab che lo consenta.</p>",
        "<p>Smonti sempre i supporti rimovibili prima di scollegarli. Rox-Filer2 "
        "non scrive su un dispositivo dopo la chiusura della sua finestra, ma il "
        "kernel può ancora trattenere dati non scritti.</p>",
    ]),
    "images": ("Immagini disco (ISO, IMG, SFS, SquashFS)", [
        '<p>Faccia clic destro su un file immagine e scelga <b>Monta immagine</b>. Rox-Filer2 la associa a un dispositivo di loop e la monta in sola lettura, poi apre il risultato come qualsiasi altra cartella. Le estensioni supportate sono <code>.iso</code>, <code>.img</code>, <code>.sfs</code>, <code>.squashfs</code>, <code>.sqfs</code> e <code>.sqsh</code>.</p>',
        '<p>Una volta montata, lo stesso menu offre <b>Apri immagine montata</b> e <b>Smonta immagine</b>. I montaggi si trovano sotto <code>/media/rox-filer2-images/</code>, una cartella per immagine.</p>',
        "<ul><li><b>Sempre in sola lettura.</b> Il dispositivo di loop, il dispositivo a blocchi e il montaggio sono tutti impostati in sola lettura, quindi un'immagine non può mai essere modificata per errore.</li><li><b>Come root</b> (il modello abituale di Puppy ed Essora) usa direttamente <code>losetup</code> e <code>mount</code>. Come utente normale usa <code>udisksctl</code>, che deve essere installato.</li><li><b>I file <code>.img</code> grezzi</b> possono contenere una tabella delle partizioni. Quando ne viene trovata più di una, Rox-Filer2 chiede quale montare e mostra il numero reale di ogni partizione, il file system, la dimensione e l'etichetta. Le immagini ISO e SquashFS vengono sempre montate intere, anche quando una ISO ibrida espone una tabella delle partizioni.</li><li><b>Senza GVfs.</b> L'immagine diventa un normale montaggio locale, quindi copia, trascinamento, tipi MIME e azioni personalizzate si comportano come in qualsiasi altra cartella.</li></ul>",
        '<p>Il montaggio avviene in secondo piano: compare una piccola finestra con un indicatore rotante mentre il dispositivo di loop viene preparato e le partizioni analizzate, e il gestore resta reattivo.</p>',
        "<p>Smonti prima di eliminare o spostare il file immagine. Se un montaggio scompare all'insaputa di Rox-Filer2, le voci del menu tornano a offrire <b>Monta immagine</b>.</p>",
    ]),

    "network": ("SMB e condivisioni Windows", [
        "<p>Apra <b>Vai &gt; Connetti a condivisione SMB</b>, oppure "
        "<b>Esplora rete</b> nella barra laterale Moderna. Inserisca il server e, "
        "se lo conosce, la condivisione; il pulsante di elenco chiede al server "
        "quali condivisioni offre.</p>",
        "<p>Rox-Filer2 monta la condivisione come un normale montaggio locale "
        "tramite <code>mount.cifs</code>, invece di presentarla attraverso un "
        "file system virtuale. Per questo copia, trascinamento, tipi MIME, "
        "Cestino e azioni personalizzate funzionano esattamente come sui file "
        "locali.</p>",
        "<ul>"
        "<li>Deve essere installato <code>cifs-utils</code>.</li>"
        "<li>Il montaggio richiede root. Come utente normale, Rox-Filer2 tenta "
        "<code>sudo -n</code>; una regola senza password per "
        "<code>mount.cifs</code> equivale a concedere root, quindi la conceda "
        "consapevolmente o esegua il montaggio da sé.</li>"
        "<li>La sua password non viene mai scritta nella configurazione. Passa "
        "per un file temporaneo di credenziali leggibile solo da lei, eliminato "
        "non appena <code>mount.cifs</code> termina. Vengono ricordati solo "
        "server, condivisione, utente e dominio.</li>"
        "</ul>",
    ]),
    "trash": ("Cestino", [
        "<p>Eliminare dal menu sposta gli elementi nel Cestino XDG, così possono "
        "essere ripristinati. Ripristini dal menu contestuale della cartella del "
        "Cestino.</p>",
        "<p>Il Cestino funziona solo all'interno dello stesso file system della "
        "cartella cestino. Eliminare un file su un'altra partizione o su un "
        "dispositivo rimovibile può essere irreversibile, e Rox-Filer2 la avvisa "
        "quando accade.</p>",
    ]),
    "mime": ("Tipi di file e Apri con", [
        "<p>I tipi di file provengono dal database MIME condiviso, più eventuali "
        "attributi estesi impostati sul file stesso. Il menu contestuale elenca "
        "le applicazioni registrate per quel tipo sotto <b>Apri con</b>.</p>",
        "<p>Imposti un gestore permanente, o un'icona personalizzata, dalle voci "
        "<b>Imposta icona</b> e <b>Imposta azione</b> dell'elemento. Vengono "
        "salvate nella sua configurazione e non modificano mai il database di "
        "sistema.</p>",
    ]),
    "actions": ("Azioni personalizzate e modelli", [
        "<p>Le azioni personalizzate aggiungono i suoi comandi al menu "
        "contestuale, ricevendo i percorsi selezionati come argomenti. Sono "
        "normali voci desktop, quindi può copiarne una da una macchina "
        "all'altra.</p>",
        "<p>I modelli permettono di creare dal menu contestuale un nuovo file "
        "vuoto di un dato tipo. Aggiunga i suoi collocando un file nella cartella "
        "Templates della cartella dell'applicazione.</p>",
    ]),
    "tools": ("Ricerca, segnalibri e finestre appaiate", [
        "<ul>"
        "<li><b>Ricerca:</b> <code>rox-find</code> cerca per nome, dimensione, "
        "tipo e contenuto, e restituisce i risultati al gestore.</li>"
        "<li><b>Segnalibri:</b> tengono a un clic le cartelle usate spesso; in "
        "Moderna compaiono anche sotto Risorse.</li>"
        "<li><b>Finestre appaiate:</b> <code>rox --pair</code> apre due finestre "
        "affiancate per copiare tra cartelle, e <code>rox --pair-realign</code> "
        "le riallinea.</li>"
        "</ul>",
    ]),
    "options": ("Opzioni e file di configurazione", [
        "<p>Apra <b>Opzioni</b> dal menu Modifica, dal menu della scrivania o con "
        "<code>rox --config-rox</code>.</p>",
        "<p>Le sue impostazioni risiedono nella cartella di configurazione XDG, "
        "normalmente <code>~/.config/rox.sourceforge.net/ROX-Filer/</code>. I "
        "file sono XML, INI e testo semplice, quindi possono essere salvati, "
        "modificati o copiati tra macchine. Eliminare un file ripristina quel "
        "gruppo di valori predefiniti.</p>",
    ]),
    "cmdline": ("Riga di comando", [
        "<ul>"
        "<li><code>rox</code> &mdash; sceglie automaticamente il backend X11 o "
        "Wayland.</li>"
        "<li><code>rox-x11</code>, <code>rox-wayland</code> &mdash; forzano un "
        "backend.</li>"
        "<li><code>rox DIR</code> &mdash; apre una cartella.</li>"
        "<li><code>rox -d DIR</code> &mdash; la apre come cartella anche se è una "
        "cartella di applicazione.</li>"
        "<li><code>rox -D DIR</code> &mdash; chiude quella cartella e le sue "
        "sottocartelle.</li>"
        "<li><code>rox --classic</code>, <code>rox --modern</code> &mdash; "
        "sostituzione temporanea dell'interfaccia.</li>"
        "<li><code>rox --desktop</code> &mdash; avvia la scrivania; "
        "<code>--desktop-refresh</code> aggiorna quella in esecuzione.</li>"
        "<li><code>rox --help</code> &mdash; l'elenco completo.</li>"
        "</ul>",
    ]),
    "diagnostics": ("Diagnostica", [
        "<p>La registrazione è disattivata in modo predefinito. Avvii con "
        "<code>rox --debug</code> per scrivere un registro tecnico, "
        "<code>--log-file=FILE</code> per scegliere dove, e "
        "<code>--log-level=LIVELLO</code> tra error, warning, info, debug o "
        "trace. <code>rox --clear-logs</code> rimuove i registri gestiti "
        "automaticamente da Rox-Filer2.</p>",
        "<p>Alleghi quel registro quando segnala un problema. Annota quale "
        "backend della scrivania è stato scelto, quale stile di interfaccia è "
        "attivo e perché è fallito un montaggio SMB o una scansione delle "
        "unità.</p>",
    ]),
    "support": ("Segnalare problemi", [
        "<p>Segnali errori e suggerimenti sulla pagina del progetto, "
        "<a href=\"https://github.com/josejp2424/ROX-Filer-gtk3\">"
        "github.com/josejp2424/ROX-Filer-gtk3</a>.</p>",
        "<p>Indichi quale versione usa (veda <b>Aiuto &gt; Informazioni su "
        "Rox-Filer2</b>), se usa X11 o Wayland, quale stile di interfaccia e i "
        "passi esatti che riproducono il problema.</p>",
    ]),
}

T["ca"] = {
    "_title": "Manual del Rox-Filer2",
    "_sub": "Gestor de fitxers ràpid i lleuger per a X11 i Wayland, continuació del ROX-Filer.",
    "_toc": "Contingut",
    "_footer": ("El Rox-Filer2 és programari lliure distribuït sota la Llicència "
                "Pública General de GNU, versió 2 o posterior. ROX-Filer "
                "original &copy; 2005 Thomas Leonard i col·laboradors; "
                "continuació Rox-Filer2 &copy; 2026 josejp2424."),
    "intro": ("Què és el Rox-Filer2", [
        "<p>El Rox-Filer2 és la continuació del ROX-Filer, actualitzada a GTK3 i "
        "ampliada amb una interfície moderna, suport SMB natiu, icones d'unitats "
        "i un mode d'escriptori que funciona tant a X11 com a Wayland.</p>",
        "<p>Fa una sola cosa en un sol procés: gestiona fitxers i, si voleu, "
        "l'escriptori. No hi ha cap dimoni, ni capa de sistema de fitxers "
        "virtual, ni dependència d'un entorn d'escriptori. Tot el que veieu són "
        "camins POSIX corrents.</p>",
    ]),
    "interfaces": ("Interfícies Clàssica i Moderna", [
        "<p>El Rox-Filer2 inclou dos estils d'interfície que comparteixen els "
        "mateixos motors de fitxers, MIME, muntatge, Paperera i Escriptori.</p>",
        "<ul>"
        "<li><b>Clàssica ROX</b> conserva la disposició i la barra d'eines "
        "tradicionals del ROX-Filer. Si veniu del ROX-Filer, res no s'ha "
        "mogut.</li>"
        "<li><b>Moderna</b> afegeix barra de menús, fila de navegació, pestanyes "
        "i una barra lateral amb Llocs, Dispositius i Xarxa.</li>"
        "</ul>",
        "<p>Trieu l'estil permanent a <b>Opcions &gt; Interfície</b>. El canvi "
        "s'aplica a les finestres obertes després de reiniciar el Rox-Filer2.</p>",
        "<p>Per provar un estil una sola vegada sense canviar la preferència "
        "desada, executeu <code>rox --classic</code> o <code>rox --modern</code>.</p>",
    ]),
    "window": ("La finestra del gestor", [
        "<p>Obriu els directoris amb un doble clic. Un sol clic selecciona; "
        "arrossegueu amb el botó esquerre sobre espai buit per seleccionar amb "
        "goma elàstica.</p>",
        "<ul>"
        "<li>El botó dret obre el menú contextual de l'element sota el punter, o "
        "el de tota la finestra si sou sobre espai buit.</li>"
        "<li>El botó del mig obre un directori en una finestra nova.</li>"
        "<li><kbd>Retrocés</kbd> puja un nivell; <kbd>Ctrl+L</kbd> enfoca "
        "l'entrada de camí.</li>"
        "<li>A la Moderna, <kbd>Ctrl+T</kbd> obre una pestanya i <kbd>Ctrl+W</kbd> "
        "la tanca. Cada pestanya manté el seu camí i historial.</li>"
        "</ul>",
        "<p>Canvieu entre la vista d'icones i la vista detallada amb el selector "
        "de vista, i ajusteu la mida de les icones amb el lliscador.</p>",
    ]),
    "desktop": ("Escriptori ROX", [
        "<p>Inicieu l'escriptori amb <code>rox --desktop</code>. Només s'executa "
        "una instància alhora. Dibuixa el fons de pantalla, mostra les icones del "
        "vostre directori d'escriptori i pot mostrar icones d'unitats.</p>",
        "<ul>"
        "<li><b>Fons de pantalla:</b> <code>rox --desktop-wallpaper</code>, o "
        "clic dret a l'escriptori. Els modes són omplir, ajustar, estirar, "
        "centrar i mosaic.</li>"
        "<li><b>Aplicacions:</b> <code>rox --desktop-apps</code> gestiona els "
        "llançadors de l'escriptori.</li>"
        "<li><b>Icones d'unitats:</b> "
        "<code>rox --desktop-drive-icon-layout</code> les ordena.</li>"
        "<li><b>Preferències:</b> <code>rox --desktop-preferences</code>.</li>"
        "</ul>",
        "<p>A X11 l'escriptori fa servir una finestra normal amb suggeriment de "
        "tipus escriptori. A Wayland utilitza la capa de fons del Layer Shell, "
        "que requereix <code>gtk-layer-shell</code> instal·lat i un compositor "
        "que publiqui <code>zwlr_layer_shell_v1</code>, com el Labwc. Forceu un "
        "rerefons amb <code>rox-x11 --desktop</code> o "
        "<code>rox-wayland --desktop</code>.</p>",
        "<p>Per fixar també el fons de la finestra arrel d'X11, de manera que els "
        "terminals amb transparència falsa i el conky vegin la mateixa imatge, "
        "instal·leu <code>feh</code> o <code>xwallpaper</code>.</p>",
    ]),
    "drives": ("Unitats i muntatge", [
        "<p>Les unitats locals i extraïbles apareixen a la barra lateral de la "
        "Moderna sota <b>Dispositius</b> i, opcionalment, com a icones a "
        "l'escriptori. Feu clic en una unitat per muntar-la i obrir-la; feu "
        "servir el menú contextual per desmuntar-la.</p>",
        "<p>La informació de les unitats prové de <code>lsblk</code>, de "
        "<code>/proc/mounts</code> i de sysfs, de manera que no cal cap servei en "
        "segon pla. Muntar una unitat com a usuari normal requereix "
        "<code>udisksctl</code> o una entrada a fstab que ho permeti.</p>",
        "<p>Desmunteu sempre els suports extraïbles abans de desconnectar-los. El "
        "Rox-Filer2 no escriu en un dispositiu després que tanqueu la seva "
        "finestra, però el nucli pot conservar dades sense escriure.</p>",
    ]),
    "images": ("Imatges de disc (ISO, IMG, SFS, SquashFS)", [
        "<p>Feu clic dret sobre un fitxer d'imatge i trieu <b>Munta la imatge</b>. El Rox-Filer2 l'associa a un dispositiu de bucle i la munta en només lectura, i després obre el resultat com qualsevol altre directori. Les extensions admeses són <code>.iso</code>, <code>.img</code>, <code>.sfs</code>, <code>.squashfs</code>, <code>.sqfs</code> i <code>.sqsh</code>.</p>",
        '<p>Un cop muntada, el mateix menú ofereix <b>Obre la imatge muntada</b> i <b>Desmunta la imatge</b>. Els muntatges viuen sota <code>/media/rox-filer2-images/</code>, un directori per imatge.</p>',
        "<ul><li><b>Sempre en només lectura.</b> El dispositiu de bucle, el dispositiu de blocs i el muntatge estan tots en només lectura, de manera que una imatge no es pot modificar mai per accident.</li><li><b>Com a root</b> (el model habitual del Puppy i de l'Essora) fa servir <code>losetup</code> i <code>mount</code> directament. Com a usuari normal fa servir <code>udisksctl</code>, que ha d'estar instal·lat.</li><li><b>Els <code>.img</code> crus</b> poden contenir una taula de particions. Quan se'n troba més d'una, el Rox-Filer2 pregunta quina muntar i mostra el número real de cada partició, el seu sistema de fitxers, la mida i l'etiqueta. Les imatges ISO i SquashFS es munten sempre senceres, fins i tot quan una ISO híbrida exposa una taula de particions.</li><li><b>Sense GVfs.</b> La imatge esdevé un muntatge local corrent, així que copiar, arrossegar i deixar anar, els tipus MIME i les accions personalitzades es comporten igual que a qualsevol altre directori.</li></ul>",
        "<p>El muntatge es fa en segon pla: apareix una finestra petita amb un indicador giratori mentre es prepara el dispositiu de bucle i s'exploren les particions, i el gestor continua responent.</p>",
        "<p>Desmunteu abans d'esborrar o moure el fitxer d'imatge. Si un muntatge desapareix sense que el Rox-Filer2 ho sàpiga, les entrades del menú tornen a oferir <b>Munta la imatge</b>.</p>",
    ]),

    "network": ("SMB i recursos compartits de Windows", [
        "<p>Obriu <b>Vés &gt; Connecta a un recurs SMB</b>, o <b>Explora la "
        "xarxa</b> a la barra lateral de la Moderna. Escriviu el servidor i, si "
        "el coneixeu, el recurs; el botó de llista pregunta al servidor quins "
        "recursos ofereix.</p>",
        "<p>El Rox-Filer2 munta el recurs com un muntatge local corrent mitjançant "
        "<code>mount.cifs</code>, en lloc de presentar-lo a través d'un sistema "
        "de fitxers virtual. Per això copiar, arrossegar i deixar anar, els tipus "
        "MIME, la Paperera i les accions personalitzades funcionen exactament "
        "igual que amb fitxers locals.</p>",
        "<ul>"
        "<li>Cal tenir instal·lat <code>cifs-utils</code>.</li>"
        "<li>Muntar requereix root. Com a usuari normal, el Rox-Filer2 prova "
        "<code>sudo -n</code>; una regla sense contrasenya per a "
        "<code>mount.cifs</code> equival a concedir root, així que concediu-la "
        "conscientment o feu el muntatge vosaltres mateixos.</li>"
        "<li>La vostra contrasenya no s'escriu mai a la configuració. Passa per "
        "un fitxer temporal de credencials llegible només per vosaltres, que "
        "s'esborra tan bon punt <code>mount.cifs</code> acaba. Només es recorden "
        "el servidor, el recurs, l'usuari i el domini.</li>"
        "</ul>",
    ]),
    "trash": ("Paperera", [
        "<p>Suprimir des del menú mou els elements a la Paperera XDG, de manera "
        "que es poden restaurar. Restaureu des del menú contextual del directori "
        "de la Paperera.</p>",
        "<p>La Paperera només funciona dins del mateix sistema de fitxers que el "
        "directori de paperera. Suprimir un fitxer en una altra partició o en un "
        "dispositiu extraïble pot ser irreversible, i el Rox-Filer2 us ho avisa "
        "quan passa.</p>",
    ]),
    "mime": ("Tipus de fitxer i Obre amb", [
        "<p>Els tipus de fitxer provenen de la base de dades MIME compartida, més "
        "qualsevol atribut estès posat al fitxer mateix. El menú contextual "
        "llista les aplicacions registrades per a aquest tipus sota "
        "<b>Obre amb</b>.</p>",
        "<p>Fixeu un gestor permanent, o una icona pròpia, des de les entrades "
        "<b>Estableix la icona</b> i <b>Estableix l'acció</b> de l'element. Es "
        "desen a la vostra pròpia configuració i no modifiquen mai la base de "
        "dades del sistema.</p>",
    ]),
    "actions": ("Accions personalitzades i plantilles", [
        "<p>Les accions personalitzades afegeixen les vostres ordres al menú "
        "contextual, rebent els camins seleccionats com a arguments. Són entrades "
        "d'escriptori corrents, així que en podeu copiar una entre màquines.</p>",
        "<p>Les plantilles permeten crear des del menú contextual un fitxer nou i "
        "buit d'un tipus determinat. Afegiu les vostres deixant un fitxer al "
        "directori Templates del directori de l'aplicació.</p>",
    ]),
    "tools": ("Cerca, adreces d'interès i finestres aparellades", [
        "<ul>"
        "<li><b>Cerca:</b> el <code>rox-find</code> cerca per nom, mida, tipus i "
        "contingut, i retorna els resultats al gestor.</li>"
        "<li><b>Adreces d'interès:</b> mantenen a un clic els directoris d'ús "
        "freqüent; a la Moderna també apareixen a Llocs.</li>"
        "<li><b>Finestres aparellades:</b> <code>rox --pair</code> obre dues "
        "finestres de costat per copiar entre directoris, i "
        "<code>rox --pair-realign</code> les torna a alinear.</li>"
        "</ul>",
    ]),
    "options": ("Opcions i fitxers de configuració", [
        "<p>Obriu <b>Opcions</b> des del menú Edita, des del menú de l'escriptori "
        "o amb <code>rox --config-rox</code>.</p>",
        "<p>Els vostres paràmetres viuen al directori de configuració XDG, "
        "normalment <code>~/.config/rox.sourceforge.net/ROX-Filer/</code>. Els "
        "fitxers són XML, INI i text pla, així que els podeu desar, editar o "
        "copiar entre màquines. Suprimir un fitxer restaura aquell grup de valors "
        "predeterminats.</p>",
    ]),
    "cmdline": ("Línia d'ordres", [
        "<ul>"
        "<li><code>rox</code> &mdash; tria automàticament el rerefons X11 o "
        "Wayland.</li>"
        "<li><code>rox-x11</code>, <code>rox-wayland</code> &mdash; forcen un "
        "rerefons.</li>"
        "<li><code>rox DIR</code> &mdash; obre un directori.</li>"
        "<li><code>rox -d DIR</code> &mdash; l'obre com a directori encara que "
        "sigui un directori d'aplicació.</li>"
        "<li><code>rox -D DIR</code> &mdash; tanca aquell directori i els seus "
        "subdirectoris.</li>"
        "<li><code>rox --classic</code>, <code>rox --modern</code> &mdash; "
        "sobreescriptura temporal de la interfície.</li>"
        "<li><code>rox --desktop</code> &mdash; inicia l'escriptori; "
        "<code>--desktop-refresh</code> refresca el que ja s'executa.</li>"
        "<li><code>rox --help</code> &mdash; la llista completa.</li>"
        "</ul>",
    ]),
    "diagnostics": ("Diagnòstic", [
        "<p>El registre està desactivat per defecte. Inicieu amb "
        "<code>rox --debug</code> per escriure un registre tècnic, "
        "<code>--log-file=FITXER</code> per triar on, i "
        "<code>--log-level=NIVELL</code> amb error, warning, info, debug o trace. "
        "<code>rox --clear-logs</code> esborra els registres que el Rox-Filer2 "
        "gestiona automàticament.</p>",
        "<p>Adjunteu aquest registre quan informeu d'un problema. Anota quin "
        "rerefons d'escriptori s'ha triat, quin estil d'interfície és actiu i per "
        "què ha fallat un muntatge SMB o una exploració d'unitats.</p>",
    ]),
    "support": ("Informar de problemes", [
        "<p>Informeu d'errors i suggeriments a la pàgina del projecte, "
        "<a href=\"https://github.com/josejp2424/ROX-Filer-gtk3\">"
        "github.com/josejp2424/ROX-Filer-gtk3</a>.</p>",
        "<p>Indiqueu quina versió feu servir (vegeu <b>Ajuda &gt; Quant al "
        "Rox-Filer2</b>), si feu servir X11 o Wayland, quin estil d'interfície i "
        "els passos exactes que reprodueixen el problema.</p>",
    ]),
}

T["de"] = {
    "_title": "Rox-Filer2-Handbuch",
    "_sub": "Schneller, ressourcenschonender Dateimanager für X11 und Wayland, Fortführung von ROX-Filer.",
    "_toc": "Inhalt",
    "_footer": ("Rox-Filer2 ist freie Software unter der GNU General Public "
                "License, Version 2 oder später. Ursprüngliches ROX-Filer "
                "&copy; 2005 Thomas Leonard und Mitwirkende; Fortführung "
                "Rox-Filer2 &copy; 2026 josejp2424."),
    "intro": ("Was ist Rox-Filer2", [
        "<p>Rox-Filer2 ist die Fortführung von ROX-Filer, auf GTK3 aktualisiert "
        "und erweitert um eine moderne Oberfläche, native SMB-Unterstützung, "
        "Laufwerkssymbole und einen Desktop-Modus, der sowohl unter X11 als auch "
        "unter Wayland läuft.</p>",
        "<p>Es erledigt eine Aufgabe in einem Prozess: es verwaltet Dateien und "
        "auf Wunsch den Desktop. Es gibt keinen Dienst im Hintergrund, keine "
        "virtuelle Dateisystemschicht und keine Abhängigkeit von einer "
        "Desktop-Umgebung. Alles, was Sie sehen, sind gewöhnliche POSIX-Pfade.</p>",
    ]),
    "interfaces": ("Oberflächen Klassisch und Modern", [
        "<p>Rox-Filer2 bringt zwei Oberflächenstile mit, die dieselben Bausteine "
        "für Dateien, MIME, Einhängen, Papierkorb und Desktop verwenden.</p>",
        "<ul>"
        "<li><b>Klassisch ROX</b> behält Aufbau und Werkzeugleiste des "
        "ursprünglichen ROX-Filer bei. Wer von ROX-Filer kommt, findet alles am "
        "gewohnten Platz.</li>"
        "<li><b>Modern</b> ergänzt Menüleiste, Navigationszeile, Reiter und eine "
        "Seitenleiste mit Orte, Geräte und Netzwerk.</li>"
        "</ul>",
        "<p>Den dauerhaften Stil wählen Sie unter <b>Optionen &gt; Oberfläche</b>. "
        "Die Änderung gilt für Fenster, die Sie nach einem Neustart von "
        "Rox-Filer2 öffnen.</p>",
        "<p>Um einen Stil einmalig auszuprobieren, ohne die gespeicherte "
        "Einstellung zu ändern, starten Sie <code>rox --classic</code> oder "
        "<code>rox --modern</code>.</p>",
    ]),
    "window": ("Das Dateifenster", [
        "<p>Verzeichnisse öffnen Sie mit einem Doppelklick. Ein einfacher Klick "
        "wählt aus; Ziehen mit der linken Maustaste auf freier Fläche zieht einen "
        "Auswahlrahmen auf.</p>",
        "<ul>"
        "<li>Die rechte Maustaste öffnet das Kontextmenü des Objekts unter dem "
        "Zeiger, auf freier Fläche das des ganzen Fensters.</li>"
        "<li>Die mittlere Maustaste öffnet ein Verzeichnis in einem neuen "
        "Fenster.</li>"
        "<li><kbd>Rücktaste</kbd> geht eine Ebene höher; <kbd>Strg+L</kbd> "
        "aktiviert die Pfadeingabe.</li>"
        "<li>In Modern öffnet <kbd>Strg+T</kbd> einen Reiter und "
        "<kbd>Strg+W</kbd> schließt ihn. Jeder Reiter behält Pfad und Verlauf für "
        "sich.</li>"
        "</ul>",
        "<p>Zwischen Symbolansicht und Detailansicht wechseln Sie über den "
        "Ansichtsschalter; die Symbolgröße stellen Sie mit dem Schieberegler "
        "ein.</p>",
    ]),
    "desktop": ("ROX-Desktop", [
        "<p>Starten Sie den Desktop mit <code>rox --desktop</code>. Es läuft "
        "immer nur eine Instanz. Sie zeichnet den Hintergrund, zeigt die Symbole "
        "Ihres Desktop-Verzeichnisses und kann Laufwerkssymbole anzeigen.</p>",
        "<ul>"
        "<li><b>Hintergrundbild:</b> <code>rox --desktop-wallpaper</code> oder "
        "Rechtsklick auf den Desktop. Die Modi sind füllen, einpassen, strecken, "
        "zentrieren und kacheln.</li>"
        "<li><b>Anwendungen:</b> <code>rox --desktop-apps</code> verwaltet die "
        "Starter auf dem Desktop.</li>"
        "<li><b>Laufwerkssymbole:</b> "
        "<code>rox --desktop-drive-icon-layout</code> ordnet sie an.</li>"
        "<li><b>Einstellungen:</b> <code>rox --desktop-preferences</code>.</li>"
        "</ul>",
        "<p>Unter X11 verwendet der Desktop ein normales Fenster mit "
        "Desktop-Typhinweis. Unter Wayland nutzt er die Hintergrundebene von "
        "Layer Shell; dafür müssen <code>gtk-layer-shell</code> installiert und "
        "ein Compositor vorhanden sein, der <code>zwlr_layer_shell_v1</code> "
        "bereitstellt, etwa Labwc. Ein Backend erzwingen Sie mit "
        "<code>rox-x11 --desktop</code> oder <code>rox-wayland --desktop</code>.</p>",
        "<p>Damit auch der X11-Wurzelfensterhintergrund gesetzt wird und "
        "pseudotransparente Terminals sowie conky dasselbe Bild sehen, "
        "installieren Sie <code>feh</code> oder <code>xwallpaper</code>.</p>",
    ]),
    "drives": ("Laufwerke und Einhängen", [
        "<p>Lokale und Wechseldatenträger erscheinen in der modernen "
        "Seitenleiste unter <b>Geräte</b> und wahlweise als Desktop-Symbole. Ein "
        "Klick hängt das Laufwerk ein und öffnet es; über das Kontextmenü hängen "
        "Sie es wieder aus.</p>",
        "<p>Die Laufwerksdaten stammen aus <code>lsblk</code>, aus "
        "<code>/proc/mounts</code> und aus sysfs, ein Hintergrunddienst ist also "
        "nicht nötig. Das Einhängen als gewöhnlicher Benutzer erfordert "
        "<code>udisksctl</code> oder einen entsprechenden fstab-Eintrag.</p>",
        "<p>Hängen Sie Wechseldatenträger immer aus, bevor Sie sie abziehen. "
        "Rox-Filer2 schreibt nach dem Schließen des Fensters nicht mehr auf das "
        "Gerät, der Kernel kann aber noch ungeschriebene Daten halten.</p>",
    ]),
    "images": ("Abbilddateien (ISO, IMG, SFS, SquashFS)", [
        '<p>Klicken Sie mit der rechten Maustaste auf eine Abbilddatei und wählen Sie <b>Abbild einhängen</b>. Rox-Filer2 verbindet sie mit einem Loop-Gerät, hängt sie schreibgeschützt ein und öffnet das Ergebnis wie jedes andere Verzeichnis. Unterstützte Endungen sind <code>.iso</code>, <code>.img</code>, <code>.sfs</code>, <code>.squashfs</code>, <code>.sqfs</code> und <code>.sqsh</code>.</p>',
        '<p>Nach dem Einhängen bietet dasselbe Menü <b>Eingehängtes Abbild öffnen</b> und <b>Abbild aushängen</b>. Die Einhängepunkte liegen unter <code>/media/rox-filer2-images/</code>, ein Verzeichnis je Abbild.</p>',
        '<ul><li><b>Immer schreibgeschützt.</b> Loop-Gerät, Blockgerät und Einhängung sind alle schreibgeschützt, ein Abbild kann also nie versehentlich verändert werden.</li><li><b>Als root</b> (das übliche Modell von Puppy und Essora) werden <code>losetup</code> und <code>mount</code> direkt verwendet. Als gewöhnlicher Benutzer wird <code>udisksctl</code> benutzt, das installiert sein muss.</li><li><b>Rohe <code>.img</code>-Dateien</b> können eine Partitionstabelle enthalten. Werden mehrere Partitionen gefunden, fragt Rox-Filer2 nach und zeigt zu jeder die tatsächliche Nummer, das Dateisystem, die Größe und die Bezeichnung. ISO- und SquashFS-Abbilder werden immer als Ganzes eingehängt, auch wenn ein hybrides ISO eine Partitionstabelle zeigt.</li><li><b>Kein GVfs.</b> Das Abbild wird zu einer gewöhnlichen lokalen Einhängung, sodass Kopieren, Ziehen und Ablegen, MIME-Behandlung und eigene Aktionen sich genau wie sonst verhalten.</li></ul>',
        '<p>Das Einhängen läuft im Hintergrund: Ein kleines Fenster mit einer Aktivitätsanzeige erscheint, während das Loop-Gerät eingerichtet und die Partitionen erfasst werden; der Dateimanager bleibt bedienbar.</p>',
        '<p>Hängen Sie aus, bevor Sie die Abbilddatei löschen oder verschieben. Verschwindet eine Einhängung ohne Zutun von Rox-Filer2, bieten die Menüeinträge wieder <b>Abbild einhängen</b> an.</p>',
    ]),

    "network": ("SMB und Windows-Freigaben", [
        "<p>Öffnen Sie <b>Gehe zu &gt; Mit SMB-Freigabe verbinden</b> oder "
        "<b>Netzwerk durchsuchen</b> in der modernen Seitenleiste. Geben Sie den "
        "Server und, falls bekannt, die Freigabe an; die Listen-Schaltfläche "
        "fragt den Server, welche Freigaben er anbietet.</p>",
        "<p>Rox-Filer2 hängt die Freigabe mit <code>mount.cifs</code> als "
        "gewöhnlichen lokalen Mount ein, statt sie über ein virtuelles Dateisystem "
        "darzustellen. Deshalb funktionieren Kopieren, Ziehen und Ablegen, "
        "MIME-Behandlung, Papierkorb und eigene Aktionen genau wie bei lokalen "
        "Dateien.</p>",
        "<ul>"
        "<li><code>cifs-utils</code> muss installiert sein.</li>"
        "<li>Einhängen erfordert Root-Rechte. Als gewöhnlicher Benutzer versucht "
        "Rox-Filer2 <code>sudo -n</code>; eine passwortlose Regel für "
        "<code>mount.cifs</code> kommt der Vergabe von Root gleich, erteilen Sie "
        "sie also bewusst oder hängen Sie selbst ein.</li>"
        "<li>Ihr Passwort wird nie in die Konfiguration geschrieben. Es läuft "
        "über eine temporäre Anmeldedatei, die nur Sie lesen können und die "
        "gelöscht wird, sobald <code>mount.cifs</code> zurückkehrt. Gespeichert "
        "werden nur Server, Freigabe, Benutzer und Domäne.</li>"
        "</ul>",
    ]),
    "trash": ("Papierkorb", [
        "<p>Löschen über das Menü verschiebt Objekte in den XDG-Papierkorb, "
        "sodass sie wiederhergestellt werden können. Wiederherstellen können Sie "
        "über das Kontextmenü des Papierkorbverzeichnisses.</p>",
        "<p>Der Papierkorb funktioniert nur innerhalb desselben Dateisystems wie "
        "das Papierkorbverzeichnis. Das Löschen einer Datei auf einer anderen "
        "Partition oder einem Wechseldatenträger ist unter Umständen nicht "
        "umkehrbar; Rox-Filer2 weist Sie in diesem Fall darauf hin.</p>",
    ]),
    "mime": ("Dateitypen und Öffnen mit", [
        "<p>Die Dateitypen stammen aus der gemeinsamen MIME-Datenbank sowie aus "
        "erweiterten Attributen an der Datei selbst. Das Kontextmenü listet unter "
        "<b>Öffnen mit</b> die für diesen Typ registrierten Anwendungen auf.</p>",
        "<p>Ein dauerhaftes Programm oder ein eigenes Symbol legen Sie über die "
        "Einträge <b>Symbol festlegen</b> und <b>Aktion festlegen</b> des Objekts "
        "fest. Beides landet in Ihrer eigenen Konfiguration und verändert nie die "
        "Systemdatenbank.</p>",
    ]),
    "actions": ("Eigene Aktionen und Vorlagen", [
        "<p>Eigene Aktionen fügen dem Kontextmenü Ihre Befehle hinzu und "
        "erhalten die ausgewählten Pfade als Argumente. Es sind gewöhnliche "
        "Desktop-Einträge, Sie können eine also zwischen Rechnern kopieren.</p>",
        "<p>Vorlagen erlauben es, über das Kontextmenü eine neue leere Datei "
        "eines bestimmten Typs anzulegen. Eigene fügen Sie hinzu, indem Sie eine "
        "Datei im Verzeichnis Templates des Anwendungsverzeichnisses ablegen.</p>",
    ]),
    "tools": ("Suche, Lesezeichen und gekoppelte Fenster", [
        "<ul>"
        "<li><b>Suche:</b> <code>rox-find</code> sucht nach Name, Größe, Typ und "
        "Inhalt und gibt die Treffer an den Dateimanager zurück.</li>"
        "<li><b>Lesezeichen:</b> halten häufig genutzte Verzeichnisse einen Klick "
        "entfernt; in Modern erscheinen sie zusätzlich unter Orte.</li>"
        "<li><b>Gekoppelte Fenster:</b> <code>rox --pair</code> öffnet zwei "
        "Fenster nebeneinander zum Kopieren zwischen Verzeichnissen, "
        "<code>rox --pair-realign</code> richtet sie neu aus.</li>"
        "</ul>",
    ]),
    "options": ("Optionen und Konfigurationsdateien", [
        "<p>Öffnen Sie <b>Optionen</b> über das Menü Bearbeiten, über das "
        "Desktop-Menü oder mit <code>rox --config-rox</code>.</p>",
        "<p>Ihre Einstellungen liegen im XDG-Konfigurationsverzeichnis, "
        "normalerweise <code>~/.config/rox.sourceforge.net/ROX-Filer/</code>. Die "
        "Dateien sind XML, INI und einfacher Text und lassen sich sichern, "
        "bearbeiten oder zwischen Rechnern kopieren. Löschen Sie eine Datei, wird "
        "diese Gruppe von Voreinstellungen wiederhergestellt.</p>",
    ]),
    "cmdline": ("Befehlszeile", [
        "<ul>"
        "<li><code>rox</code> &mdash; wählt automatisch das X11- oder "
        "Wayland-Backend.</li>"
        "<li><code>rox-x11</code>, <code>rox-wayland</code> &mdash; erzwingen ein "
        "Backend.</li>"
        "<li><code>rox VERZ</code> &mdash; öffnet ein Verzeichnis.</li>"
        "<li><code>rox -d VERZ</code> &mdash; öffnet es als Verzeichnis, auch "
        "wenn es ein Anwendungsverzeichnis ist.</li>"
        "<li><code>rox -D VERZ</code> &mdash; schließt dieses Verzeichnis und "
        "seine Unterverzeichnisse.</li>"
        "<li><code>rox --classic</code>, <code>rox --modern</code> &mdash; "
        "vorübergehende Wahl der Oberfläche.</li>"
        "<li><code>rox --desktop</code> &mdash; startet den Desktop; "
        "<code>--desktop-refresh</code> aktualisiert einen laufenden.</li>"
        "<li><code>rox --help</code> &mdash; die vollständige Liste.</li>"
        "</ul>",
    ]),
    "diagnostics": ("Diagnose", [
        "<p>Die Protokollierung ist standardmäßig aus. Starten Sie mit "
        "<code>rox --debug</code>, um ein technisches Protokoll zu schreiben, mit "
        "<code>--log-file=DATEI</code> für den Ort und "
        "<code>--log-level=STUFE</code> mit error, warning, info, debug oder "
        "trace. <code>rox --clear-logs</code> entfernt die von Rox-Filer2 "
        "automatisch verwalteten Protokolle.</p>",
        "<p>Legen Sie dieses Protokoll jeder Fehlermeldung bei. Es hält fest, "
        "welches Desktop-Backend gewählt wurde, welcher Oberflächenstil aktiv ist "
        "und warum ein SMB-Mount oder ein Laufwerkssuchlauf fehlgeschlagen "
        "ist.</p>",
    ]),
    "support": ("Probleme melden", [
        "<p>Melden Sie Fehler und Vorschläge auf der Projektseite "
        "<a href=\"https://github.com/josejp2424/ROX-Filer-gtk3\">"
        "github.com/josejp2424/ROX-Filer-gtk3</a>.</p>",
        "<p>Nennen Sie dabei Ihre Version (siehe <b>Hilfe &gt; Über "
        "Rox-Filer2</b>), ob Sie X11 oder Wayland verwenden, welchen "
        "Oberflächenstil und die genauen Schritte, die das Problem "
        "reproduzieren.</p>",
    ]),
}
