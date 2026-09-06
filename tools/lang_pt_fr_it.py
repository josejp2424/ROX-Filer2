# -*- coding: utf-8 -*-
"""Traducciones pt / fr / it del manual. Importado por make-manual.py."""

T = {}

T["pt"] = {
    "_title": "Manual do Rox-Filer2",
    "_sub": "Gestor de ficheiros rápido e leve para X11 e Wayland, continuação do ROX-Filer.",
    "_toc": "Conteúdo",
    "_footer": ("O Rox-Filer2 é software livre distribuído sob a Licença Pública "
                "Geral GNU, versão 2 ou posterior. ROX-Filer original "
                "&copy; 2005 Thomas Leonard e colaboradores; continuação "
                "Rox-Filer2 &copy; 2026 josejp2424."),
    "intro": ("O que é o Rox-Filer2", [
        "<p>O Rox-Filer2 é a continuação do ROX-Filer, atualizado para GTK3 e "
        "ampliado com uma interface moderna, suporte SMB nativo, ícones de "
        "unidades e um modo de ambiente de trabalho que funciona tanto em X11 "
        "como em Wayland.</p>",
        "<p>Faz uma só coisa num só processo: gere ficheiros e, se quiser, o "
        "ambiente de trabalho. Não há serviço em segundo plano, nem camada de "
        "sistema de ficheiros virtual, nem dependência de um ambiente de "
        "trabalho. Tudo o que vê são caminhos POSIX comuns.</p>",
    ]),
    "interfaces": ("Interfaces Clássica e Moderna", [
        "<p>O Rox-Filer2 inclui dois estilos de interface que partilham os mesmos "
        "motores de ficheiros, MIME, montagem, Reciclagem e Ambiente de "
        "trabalho.</p>",
        "<ul>"
        "<li><b>Clássica ROX</b> mantém a disposição e a barra de ferramentas "
        "tradicionais do ROX-Filer. Se vem do ROX-Filer, nada mudou de "
        "lugar.</li>"
        "<li><b>Moderna</b> acrescenta barra de menus, linha de navegação, "
        "separadores e uma barra lateral com Locais, Dispositivos e Rede.</li>"
        "</ul>",
        "<p>Escolha o estilo permanente em <b>Opções &gt; Interface</b>. A "
        "alteração aplica-se às janelas abertas depois de reiniciar o "
        "Rox-Filer2.</p>",
        "<p>Para experimentar um estilo uma única vez sem alterar a preferência "
        "guardada, execute <code>rox --classic</code> ou "
        "<code>rox --modern</code>.</p>",
    ]),
    "window": ("A janela do gestor", [
        "<p>Abra diretórios com duplo clique. Um clique simples seleciona; "
        "arraste com o botão esquerdo sobre espaço vazio para selecionar em "
        "laço.</p>",
        "<ul>"
        "<li>O botão direito abre o menu de contexto do item sob o ponteiro, ou "
        "o da janela inteira se estiver sobre espaço vazio.</li>"
        "<li>O botão do meio abre um diretório numa janela nova.</li>"
        "<li><kbd>Retrocesso</kbd> sobe um nível; <kbd>Ctrl+L</kbd> foca a "
        "entrada de caminho.</li>"
        "<li>Na Moderna, <kbd>Ctrl+T</kbd> abre um separador e <kbd>Ctrl+W</kbd> "
        "fecha-o. Cada separador mantém o seu caminho e histórico.</li>"
        "</ul>",
        "<p>Alterne entre a vista de ícones e a vista detalhada com o seletor de "
        "vista, e ajuste o tamanho dos ícones com o cursor deslizante.</p>",
    ]),
    "desktop": ("Ambiente de trabalho ROX", [
        "<p>Inicie o ambiente de trabalho com <code>rox --desktop</code>. Só "
        "corre uma instância de cada vez. Desenha o fundo, mostra os ícones do "
        "seu diretório de ambiente de trabalho e pode mostrar ícones de "
        "unidades.</p>",
        "<ul>"
        "<li><b>Fundo:</b> <code>rox --desktop-wallpaper</code>, ou clique "
        "direito no ambiente de trabalho. Os modos são preencher, ajustar, "
        "esticar, centrar e mosaico.</li>"
        "<li><b>Aplicações:</b> <code>rox --desktop-apps</code> gere os "
        "lançadores do ambiente de trabalho.</li>"
        "<li><b>Ícones de unidades:</b> "
        "<code>rox --desktop-drive-icon-layout</code> organiza-os.</li>"
        "<li><b>Preferências:</b> <code>rox --desktop-preferences</code>.</li>"
        "</ul>",
        "<p>Em X11 o ambiente de trabalho usa uma janela normal com sugestão de "
        "tipo desktop. Em Wayland usa a camada de fundo do Layer Shell, que exige "
        "<code>gtk-layer-shell</code> instalado e um compositor que publique "
        "<code>zwlr_layer_shell_v1</code>, como o Labwc. Force um backend com "
        "<code>rox-x11 --desktop</code> ou <code>rox-wayland --desktop</code>.</p>",
        "<p>Para definir também o fundo da janela raiz do X11, de modo que "
        "terminais com transparência falsa e o conky vejam a mesma imagem, "
        "instale <code>feh</code> ou <code>xwallpaper</code>.</p>",
    ]),
    "drives": ("Unidades e montagem", [
        "<p>As unidades locais e amovíveis aparecem na barra lateral da Moderna "
        "em <b>Dispositivos</b> e, opcionalmente, como ícones no ambiente de "
        "trabalho. Clique numa unidade para a montar e abrir; use o menu de "
        "contexto para a desmontar.</p>",
        "<p>A informação das unidades vem do <code>lsblk</code>, de "
        "<code>/proc/mounts</code> e do sysfs, pelo que não é necessário nenhum "
        "serviço em segundo plano. Montar uma unidade como utilizador normal "
        "exige <code>udisksctl</code> ou uma entrada no fstab que o permita.</p>",
        "<p>Desmonte sempre os suportes amovíveis antes de os desligar. O "
        "Rox-Filer2 não escreve num dispositivo depois de fechar a sua janela, "
        "mas o núcleo pode reter dados por gravar.</p>",
    ]),
    "images": ("Imagens de disco (ISO, IMG, SFS, SquashFS)", [
        '<p>Clique com o botão direito num ficheiro de imagem e escolha <b>Montar imagem</b>. O Rox-Filer2 associa-a a um dispositivo de loop e monta-a apenas para leitura, abrindo depois o resultado como qualquer outro diretório. As extensões suportadas são <code>.iso</code>, <code>.img</code>, <code>.sfs</code>, <code>.squashfs</code>, <code>.sqfs</code> e <code>.sqsh</code>.</p>',
        '<p>Uma vez montada, o mesmo menu oferece <b>Abrir imagem montada</b> e <b>Desmontar imagem</b>. Os montagens ficam sob <code>/media/rox-filer2-images/</code>, um diretório por imagem.</p>',
        '<ul><li><b>Sempre apenas para leitura.</b> O dispositivo de loop, o dispositivo de bloco e a montagem são todos definidos como só de leitura, pelo que uma imagem nunca pode ser modificada por acidente.</li><li><b>Como root</b> (o modelo habitual do Puppy e do Essora) usa <code>losetup</code> e <code>mount</code> diretamente. Como utilizador normal usa <code>udisksctl</code>, que tem de estar instalado.</li><li><b>Os <code>.img</code> em bruto</b> podem conter uma tabela de partições. Quando é encontrada mais do que uma, o Rox-Filer2 pergunta qual montar e mostra o número real de cada partição, o sistema de ficheiros, o tamanho e a etiqueta. As imagens ISO e SquashFS são sempre montadas inteiras, mesmo quando uma ISO híbrida expõe uma tabela de partições.</li><li><b>Sem GVfs.</b> A imagem passa a ser uma montagem local comum, pelo que copiar, arrastar e largar, os tipos MIME e as ações personalizadas funcionam como em qualquer outro diretório.</li></ul>',
        '<p>A montagem decorre em segundo plano: surge uma pequena janela com um indicador rotativo enquanto o dispositivo de loop é preparado e as partições são analisadas, e o gestor continua a responder.</p>',
        '<p>Desmonte antes de apagar ou mover o ficheiro de imagem. Se uma montagem desaparecer sem o Rox-Filer2 saber, as entradas do menu voltam a oferecer <b>Montar imagem</b>.</p>',
    ]),

    "network": ("SMB e partilhas do Windows", [
        "<p>Abra <b>Ir &gt; Ligar a partilha SMB</b>, ou <b>Explorar a rede</b> "
        "na barra lateral da Moderna. Escreva o servidor e, se o souber, a "
        "partilha; o botão de listagem pergunta ao servidor que partilhas "
        "oferece.</p>",
        "<p>O Rox-Filer2 monta a partilha como uma montagem local comum através "
        "do <code>mount.cifs</code>, em vez de a apresentar por um sistema de "
        "ficheiros virtual. Por isso copiar, arrastar e largar, os tipos MIME, a "
        "Reciclagem e as ações personalizadas funcionam exatamente como com "
        "ficheiros locais.</p>",
        "<ul>"
        "<li>É necessário ter o <code>cifs-utils</code> instalado.</li>"
        "<li>Montar exige root. Como utilizador normal, o Rox-Filer2 tenta "
        "<code>sudo -n</code>; uma regra sem palavra-passe para "
        "<code>mount.cifs</code> equivale a conceder root, portanto conceda-a "
        "conscientemente ou faça a montagem manualmente.</li>"
        "<li>A sua palavra-passe nunca é escrita na configuração. É passada por "
        "um ficheiro temporário de credenciais legível só por si, apagado assim "
        "que o <code>mount.cifs</code> termina. Só ficam guardados o servidor, a "
        "partilha, o utilizador e o domínio.</li>"
        "</ul>",
    ]),
    "trash": ("Reciclagem", [
        "<p>Apagar pelo menu move os itens para a Reciclagem XDG, para que possam "
        "ser restaurados. Restaure a partir do menu de contexto do diretório da "
        "Reciclagem.</p>",
        "<p>A Reciclagem só funciona dentro do mesmo sistema de ficheiros do "
        "diretório de reciclagem. Apagar um ficheiro noutra partição ou num "
        "dispositivo amovível pode ser irreversível, e o Rox-Filer2 avisa-o "
        "quando isso acontece.</p>",
    ]),
    "mime": ("Tipos de ficheiro e Abrir com", [
        "<p>Os tipos de ficheiro vêm da base de dados MIME partilhada, mais "
        "qualquer atributo estendido colocado no próprio ficheiro. O menu de "
        "contexto lista as aplicações registadas para esse tipo em "
        "<b>Abrir com</b>.</p>",
        "<p>Defina um manipulador permanente, ou um ícone próprio, a partir das "
        "entradas <b>Definir ícone</b> e <b>Definir ação</b> do item. Ficam "
        "guardadas na sua própria configuração e nunca alteram a base de dados "
        "do sistema.</p>",
    ]),
    "actions": ("Ações personalizadas e modelos", [
        "<p>As ações personalizadas acrescentam os seus próprios comandos ao menu "
        "de contexto, recebendo os caminhos selecionados como argumentos. São "
        "entradas de desktop comuns, pelo que pode copiar uma entre máquinas.</p>",
        "<p>Os modelos permitem criar um ficheiro novo e vazio de um dado tipo a "
        "partir do menu de contexto. Acrescente os seus colocando um ficheiro no "
        "diretório Templates do diretório da aplicação.</p>",
    ]),
    "tools": ("Pesquisa, marcadores e janelas emparelhadas", [
        "<ul>"
        "<li><b>Pesquisa:</b> o <code>rox-find</code> procura por nome, tamanho, "
        "tipo e conteúdo, e devolve os resultados ao gestor.</li>"
        "<li><b>Marcadores:</b> mantêm a um clique os diretórios de uso "
        "frequente; na Moderna aparecem também em Locais.</li>"
        "<li><b>Janelas emparelhadas:</b> <code>rox --pair</code> abre duas "
        "janelas lado a lado para copiar entre diretórios, e "
        "<code>rox --pair-realign</code> volta a alinhá-las.</li>"
        "</ul>",
    ]),
    "options": ("Opções e ficheiros de configuração", [
        "<p>Abra <b>Opções</b> a partir do menu Editar, do menu do ambiente de "
        "trabalho ou com <code>rox --config-rox</code>.</p>",
        "<p>As suas definições ficam no diretório de configuração XDG, "
        "normalmente <code>~/.config/rox.sourceforge.net/ROX-Filer/</code>. Os "
        "ficheiros são XML, INI e texto simples, pelo que podem ser copiados, "
        "editados ou transferidos entre máquinas. Apagar um ficheiro repõe esse "
        "grupo de valores predefinidos.</p>",
    ]),
    "cmdline": ("Linha de comandos", [
        "<ul>"
        "<li><code>rox</code> &mdash; escolhe automaticamente o backend X11 ou "
        "Wayland.</li>"
        "<li><code>rox-x11</code>, <code>rox-wayland</code> &mdash; forçam um "
        "backend.</li>"
        "<li><code>rox DIR</code> &mdash; abre um diretório.</li>"
        "<li><code>rox -d DIR</code> &mdash; abre-o como diretório mesmo que seja "
        "um diretório de aplicação.</li>"
        "<li><code>rox -D DIR</code> &mdash; fecha esse diretório e os seus "
        "subdiretórios.</li>"
        "<li><code>rox --classic</code>, <code>rox --modern</code> &mdash; "
        "substituição temporária da interface.</li>"
        "<li><code>rox --desktop</code> &mdash; inicia o ambiente de trabalho; "
        "<code>--desktop-refresh</code> atualiza o que já corre.</li>"
        "<li><code>rox --help</code> &mdash; a lista completa.</li>"
        "</ul>",
    ]),
    "diagnostics": ("Diagnóstico", [
        "<p>O registo está desativado por omissão. Inicie com "
        "<code>rox --debug</code> para escrever um registo técnico, "
        "<code>--log-file=FICHEIRO</code> para escolher onde, e "
        "<code>--log-level=NÍVEL</code> com error, warning, info, debug ou trace. "
        "<code>rox --clear-logs</code> apaga os registos que o Rox-Filer2 gere "
        "automaticamente.</p>",
        "<p>Junte esse registo ao comunicar um problema. Regista que backend de "
        "ambiente de trabalho foi selecionado, que estilo de interface está ativo "
        "e por que falhou uma montagem SMB ou uma análise de unidades.</p>",
    ]),
    "support": ("Comunicar problemas", [
        "<p>Comunique erros e sugestões na página do projeto, "
        "<a href=\"https://github.com/josejp2424/ROX-Filer-gtk3\">"
        "github.com/josejp2424/ROX-Filer-gtk3</a>.</p>",
        "<p>Indique que versão usa (ver <b>Ajuda &gt; Sobre o Rox-Filer2</b>), se "
        "usa X11 ou Wayland, que estilo de interface e os passos exatos que "
        "reproduzem o problema.</p>",
    ]),
}

T["fr"] = {
    "_title": "Manuel de Rox-Filer2",
    "_sub": "Gestionnaire de fichiers rapide et léger pour X11 et Wayland, continuation de ROX-Filer.",
    "_toc": "Sommaire",
    "_footer": ("Rox-Filer2 est un logiciel libre distribué sous la Licence "
                "publique générale GNU, version 2 ou ultérieure. ROX-Filer "
                "original &copy; 2005 Thomas Leonard et contributeurs ; "
                "continuation Rox-Filer2 &copy; 2026 josejp2424."),
    "intro": ("Qu'est-ce que Rox-Filer2", [
        "<p>Rox-Filer2 est la continuation de ROX-Filer, portée sur GTK3 et "
        "enrichie d'une interface moderne, de la prise en charge native de SMB, "
        "d'icônes de périphériques et d'un mode bureau fonctionnant aussi bien "
        "sous X11 que sous Wayland.</p>",
        "<p>Il fait une seule chose dans un seul processus : il gère vos fichiers "
        "et, si vous le souhaitez, votre bureau. Aucun démon, aucune couche de "
        "système de fichiers virtuel, aucune dépendance à un environnement de "
        "bureau. Tout ce que vous voyez, ce sont des chemins POSIX ordinaires.</p>",
    ]),
    "interfaces": ("Interfaces Classique et Moderne", [
        "<p>Rox-Filer2 propose deux styles d'interface qui partagent les mêmes "
        "moteurs de fichiers, MIME, montage, Corbeille et Bureau.</p>",
        "<ul>"
        "<li><b>Classique ROX</b> conserve la disposition et la barre d'outils "
        "traditionnelles de ROX-Filer. Si vous venez de ROX-Filer, rien n'a "
        "bougé.</li>"
        "<li><b>Moderne</b> ajoute une barre de menus, une rangée de navigation, "
        "des onglets et un panneau latéral avec Emplacements, Périphériques et "
        "Réseau.</li>"
        "</ul>",
        "<p>Choisissez le style permanent dans <b>Options &gt; Interface</b>. Le "
        "changement s'applique aux fenêtres ouvertes après le redémarrage de "
        "Rox-Filer2.</p>",
        "<p>Pour essayer un style une seule fois sans modifier la préférence "
        "enregistrée, lancez <code>rox --classic</code> ou "
        "<code>rox --modern</code>.</p>",
    ]),
    "window": ("La fenêtre du gestionnaire", [
        "<p>Ouvrez les répertoires par un double-clic. Un simple clic "
        "sélectionne ; faites glisser le bouton gauche sur une zone vide pour "
        "une sélection au lasso.</p>",
        "<ul>"
        "<li>Le bouton droit ouvre le menu contextuel de l'élément sous le "
        "pointeur, ou celui de la fenêtre entière sur une zone vide.</li>"
        "<li>Le bouton du milieu ouvre un répertoire dans une nouvelle fenêtre.</li>"
        "<li><kbd>Retour arrière</kbd> remonte d'un niveau ; <kbd>Ctrl+L</kbd> "
        "active la barre d'adresse.</li>"
        "<li>En Moderne, <kbd>Ctrl+T</kbd> ouvre un onglet et <kbd>Ctrl+W</kbd> "
        "le ferme. Chaque onglet conserve son chemin et son historique.</li>"
        "</ul>",
        "<p>Basculez entre la vue en icônes et la vue détaillée grâce au sélecteur "
        "de vue, et ajustez la taille des icônes avec le curseur.</p>",
    ]),
    "desktop": ("Bureau ROX", [
        "<p>Démarrez le bureau avec <code>rox --desktop</code>. Une seule "
        "instance s'exécute à la fois. Elle dessine le fond d'écran, affiche les "
        "icônes de votre répertoire de bureau et peut afficher les icônes de "
        "périphériques.</p>",
        "<ul>"
        "<li><b>Fond d'écran :</b> <code>rox --desktop-wallpaper</code>, ou "
        "clic droit sur le bureau. Les modes sont remplir, ajuster, étirer, "
        "centrer et mosaïque.</li>"
        "<li><b>Applications :</b> <code>rox --desktop-apps</code> gère les "
        "lanceurs du bureau.</li>"
        "<li><b>Icônes de périphériques :</b> "
        "<code>rox --desktop-drive-icon-layout</code> les organise.</li>"
        "<li><b>Préférences :</b> <code>rox --desktop-preferences</code>.</li>"
        "</ul>",
        "<p>Sous X11, le bureau utilise une fenêtre normale avec l'indice de type "
        "bureau. Sous Wayland, il utilise la couche d'arrière-plan de Layer "
        "Shell, ce qui nécessite <code>gtk-layer-shell</code> et un compositeur "
        "publiant <code>zwlr_layer_shell_v1</code>, comme Labwc. Forcez un "
        "backend avec <code>rox-x11 --desktop</code> ou "
        "<code>rox-wayland --desktop</code>.</p>",
        "<p>Pour définir également le fond de la fenêtre racine X11, afin que les "
        "terminaux à fausse transparence et conky voient la même image, installez "
        "<code>feh</code> ou <code>xwallpaper</code>.</p>",
    ]),
    "drives": ("Périphériques et montage", [
        "<p>Les disques locaux et amovibles apparaissent dans le panneau latéral "
        "Moderne sous <b>Périphériques</b>, et éventuellement comme icônes du "
        "bureau. Cliquez sur un périphérique pour le monter et l'ouvrir ; "
        "utilisez le menu contextuel pour le démonter.</p>",
        "<p>Les informations proviennent de <code>lsblk</code>, de "
        "<code>/proc/mounts</code> et de sysfs : aucun service d'arrière-plan "
        "n'est requis. Monter un périphérique en tant qu'utilisateur ordinaire "
        "nécessite <code>udisksctl</code> ou une entrée fstab l'autorisant.</p>",
        "<p>Démontez toujours un support amovible avant de le débrancher. "
        "Rox-Filer2 n'écrit pas sur un périphérique après la fermeture de sa "
        "fenêtre, mais le noyau peut encore détenir des données non écrites.</p>",
    ]),
    "images": ("Images disque (ISO, IMG, SFS, SquashFS)", [
        "<p>Faites un clic droit sur un fichier image et choisissez <b>Monter l'image</b>. Rox-Filer2 l'associe à un périphérique de bouclage et la monte en lecture seule, puis ouvre le résultat comme n'importe quel répertoire. Les extensions prises en charge sont <code>.iso</code>, <code>.img</code>, <code>.sfs</code>, <code>.squashfs</code>, <code>.sqfs</code> et <code>.sqsh</code>.</p>",
        "<p>Une fois montée, le même menu propose <b>Ouvrir l'image montée</b> et <b>Démonter l'image</b>. Les montages se trouvent sous <code>/media/rox-filer2-images/</code>, un répertoire par image.</p>",
        "<ul><li><b>Toujours en lecture seule.</b> Le périphérique de bouclage, le périphérique bloc et le montage sont tous en lecture seule : une image ne peut jamais être modifiée par accident.</li><li><b>En root</b> (le modèle habituel de Puppy et d'Essora), il utilise directement <code>losetup</code> et <code>mount</code>. En utilisateur ordinaire, il utilise <code>udisksctl</code>, qui doit être installé.</li><li><b>Les fichiers <code>.img</code> bruts</b> peuvent contenir une table de partitions. Lorsque plusieurs sont trouvées, Rox-Filer2 demande laquelle monter et affiche le numéro réel de chaque partition, son système de fichiers, sa taille et son étiquette. Les images ISO et SquashFS sont toujours montées entières, même lorsqu'une ISO hybride expose une table de partitions.</li><li><b>Sans GVfs.</b> L'image devient un montage local ordinaire : la copie, le glisser-déposer, les types MIME et les actions personnalisées se comportent exactement comme ailleurs.</li></ul>",
        "<p>Le montage s'effectue en arrière-plan : une petite fenêtre avec un indicateur d'activité s'affiche pendant la préparation du périphérique de bouclage et l'analyse des partitions, et le gestionnaire reste réactif.</p>",
        "<p>Démontez avant de supprimer ou de déplacer le fichier image. Si un montage disparaît à l'insu de Rox-Filer2, les entrées du menu proposent à nouveau <b>Monter l'image</b>.</p>",
    ]),

    "network": ("SMB et partages Windows", [
        "<p>Ouvrez <b>Aller &gt; Se connecter à un partage SMB</b>, ou "
        "<b>Parcourir le réseau</b> dans le panneau latéral Moderne. Saisissez le "
        "serveur et, si vous le connaissez, le partage ; le bouton de liste "
        "demande au serveur quels partages il propose.</p>",
        "<p>Rox-Filer2 monte le partage comme un montage local ordinaire via "
        "<code>mount.cifs</code>, au lieu de le présenter par un système de "
        "fichiers virtuel. Ainsi la copie, le glisser-déposer, les types MIME, la "
        "Corbeille et les actions personnalisées fonctionnent exactement comme "
        "sur des fichiers locaux.</p>",
        "<ul>"
        "<li><code>cifs-utils</code> doit être installé.</li>"
        "<li>Le montage exige root. En utilisateur ordinaire, Rox-Filer2 tente "
        "<code>sudo -n</code> ; une règle sans mot de passe pour "
        "<code>mount.cifs</code> revient à accorder root, accordez-la donc en "
        "connaissance de cause ou effectuez le montage vous-même.</li>"
        "<li>Votre mot de passe n'est jamais écrit dans la configuration. Il "
        "transite par un fichier d'identifiants temporaire lisible par vous seul, "
        "supprimé dès que <code>mount.cifs</code> se termine. Seuls le serveur, "
        "le partage, l'utilisateur et le domaine sont mémorisés.</li>"
        "</ul>",
    ]),
    "trash": ("Corbeille", [
        "<p>Supprimer depuis le menu déplace les éléments vers la Corbeille XDG, "
        "afin de pouvoir les restaurer. Restaurez depuis le menu contextuel du "
        "répertoire de la Corbeille.</p>",
        "<p>La Corbeille ne fonctionne qu'au sein du même système de fichiers que "
        "le répertoire de corbeille. Supprimer un fichier sur une autre partition "
        "ou sur un support amovible peut être irréversible, et Rox-Filer2 vous "
        "prévient dans ce cas.</p>",
    ]),
    "mime": ("Types de fichiers et Ouvrir avec", [
        "<p>Les types de fichiers proviennent de la base MIME partagée, ainsi que "
        "de tout attribut étendu posé sur le fichier lui-même. Le menu contextuel "
        "liste les applications enregistrées pour ce type sous "
        "<b>Ouvrir avec</b>.</p>",
        "<p>Définissez un gestionnaire permanent, ou une icône personnalisée, "
        "depuis les entrées <b>Définir l'icône</b> et <b>Définir l'action</b> de "
        "l'élément. Elles sont enregistrées dans votre propre configuration et ne "
        "modifient jamais la base système.</p>",
    ]),
    "actions": ("Actions personnalisées et modèles", [
        "<p>Les actions personnalisées ajoutent vos propres commandes au menu "
        "contextuel, en recevant les chemins sélectionnés comme arguments. Ce "
        "sont des fichiers desktop ordinaires, vous pouvez donc en copier un "
        "d'une machine à l'autre.</p>",
        "<p>Les modèles permettent de créer depuis le menu contextuel un fichier "
        "vide d'un type donné. Ajoutez les vôtres en déposant un fichier dans le "
        "répertoire Templates du répertoire de l'application.</p>",
    ]),
    "tools": ("Recherche, signets et fenêtres jumelées", [
        "<ul>"
        "<li><b>Recherche :</b> <code>rox-find</code> cherche par nom, taille, "
        "type et contenu, et renvoie les résultats au gestionnaire.</li>"
        "<li><b>Signets :</b> gardent à un clic les répertoires fréquents ; en "
        "Moderne ils apparaissent aussi sous Emplacements.</li>"
        "<li><b>Fenêtres jumelées :</b> <code>rox --pair</code> ouvre deux "
        "fenêtres côte à côte pour copier entre répertoires, et "
        "<code>rox --pair-realign</code> les réaligne.</li>"
        "</ul>",
    ]),
    "options": ("Options et fichiers de configuration", [
        "<p>Ouvrez <b>Options</b> depuis le menu Édition, depuis le menu du "
        "bureau ou avec <code>rox --config-rox</code>.</p>",
        "<p>Vos réglages se trouvent dans le répertoire de configuration XDG, "
        "normalement <code>~/.config/rox.sourceforge.net/ROX-Filer/</code>. Les "
        "fichiers sont en XML, INI et texte brut : vous pouvez les sauvegarder, "
        "les éditer ou les copier d'une machine à l'autre. Supprimer un fichier "
        "restaure ce groupe de valeurs par défaut.</p>",
    ]),
    "cmdline": ("Ligne de commande", [
        "<ul>"
        "<li><code>rox</code> &mdash; choisit automatiquement le backend X11 ou "
        "Wayland.</li>"
        "<li><code>rox-x11</code>, <code>rox-wayland</code> &mdash; forcent un "
        "backend.</li>"
        "<li><code>rox DIR</code> &mdash; ouvre un répertoire.</li>"
        "<li><code>rox -d DIR</code> &mdash; l'ouvre comme répertoire même s'il "
        "s'agit d'un répertoire d'application.</li>"
        "<li><code>rox -D DIR</code> &mdash; ferme ce répertoire et ses "
        "sous-répertoires.</li>"
        "<li><code>rox --classic</code>, <code>rox --modern</code> &mdash; "
        "substitution temporaire de l'interface.</li>"
        "<li><code>rox --desktop</code> &mdash; démarre le bureau ; "
        "<code>--desktop-refresh</code> rafraîchit celui qui tourne.</li>"
        "<li><code>rox --help</code> &mdash; la liste complète.</li>"
        "</ul>",
    ]),
    "diagnostics": ("Diagnostic", [
        "<p>La journalisation est désactivée par défaut. Démarrez avec "
        "<code>rox --debug</code> pour écrire un journal technique, "
        "<code>--log-file=FICHIER</code> pour choisir l'emplacement, et "
        "<code>--log-level=NIVEAU</code> parmi error, warning, info, debug ou "
        "trace. <code>rox --clear-logs</code> supprime les journaux gérés "
        "automatiquement par Rox-Filer2.</p>",
        "<p>Joignez ce journal à tout signalement. Il indique quel backend de "
        "bureau a été retenu, quel style d'interface est actif et pourquoi un "
        "montage SMB ou une analyse de périphériques a échoué.</p>",
    ]),
    "support": ("Signaler un problème", [
        "<p>Signalez bogues et suggestions sur la page du projet, "
        "<a href=\"https://github.com/josejp2424/ROX-Filer-gtk3\">"
        "github.com/josejp2424/ROX-Filer-gtk3</a>.</p>",
        "<p>Précisez votre version (voir <b>Aide &gt; À propos de Rox-Filer2</b>), "
        "si vous utilisez X11 ou Wayland, quel style d'interface, et les étapes "
        "exactes qui reproduisent le problème.</p>",
    ]),
}
