# Tahoe OS 1.0 — Rodada 1: correções funcionais

Este documento acompanha a implementação do Tahoe OS sobre o LordOS. O trabalho acontece em uma branch separada para preservar a base funcional.

## Princípios

- Preservar o compositor, runtime, SDK, serviços e aplicações existentes.
- Corrigir a experiência e integrar recursos existentes antes de criar sistemas paralelos.
- Não presumir que uma função está ausente quando o código já a implementa; verificar a diferença entre capacidade e experiência de uso.
- Não declarar correção, build ou teste concluído sem evidência executada.
- Manter compatibilidade com touchpads com clickpad e com touchpads que possuem botões físicos separados.

## Ordem de implementação

### 1. Touchpad — prioridade máxima

**Achados atuais**

- `tp_open()` identifica o dispositivo como clickpad exclusivamente por `INPUT_PROP_BUTTONPAD`.
- A configuração secundária já oferece dois dedos, canto inferior direito e canto inferior esquerdo.
- `click_button()` só aplica a lógica dos cantos quando `TpCaps.clickpad` é verdadeiro.
- A zona inferior é calculada em milímetros, limitada a 12 mm ou 15% da altura.
- Os testes atuais cobrem cliques físicos e gestos, mas não cobrem de forma direta os três casos de clique físico de um dedo: canto inferior direito, canto inferior esquerdo e centro.
- O simulador bcm5974 declara clickpad explicitamente; isso não reproduz o caso de hardware real que omite `INPUT_PROP_BUTTONPAD`.

**Critérios de aceitação**

- Um clickpad corretamente identificado deve emitir botão direito ao pressionar o canto configurado.
- O centro deve continuar emitindo botão esquerdo.
- A configuração de dois dedos deve continuar funcionando.
- Touchpads com botões físicos separados não podem ser reclassificados como clickpads por heurística ampla.
- Testar dispositivo com e sem `INPUT_PROP_BUTTONPAD`, além de botões BTN_LEFT/BTN_RIGHT/BTN_MIDDLE.
- O evento de botão direito deve chegar ao compositor e abrir o menu contextual existente.

**Diagnóstico necessário em hardware**

Executar `tp-test --live /dev/input/eventN 10` no dispositivo real, com o touchpad identificado. Registrar também o nome, propriedades e capabilities evdev do dispositivo. O teste ao vivo deve ser feito sem deixar outro processo consumir os eventos; o comando usa EVIOCGRAB.

**Decisão de segurança**

Não inferir clickpad apenas pela ausência de BTN_RIGHT. Essa regra pode classificar incorretamente dispositivos com botão físico único. A detecção adicional deve ser baseada em capabilities consistentes e, quando necessário, quirks específicos identificados por fabricante/modelo, com override explícito nas configurações.

**Lacuna confirmada na implementação atual**

- Se `INPUT_PROP_BUTTONPAD` não estiver presente, `tp_open()` mantém `clickpad = 0`, mesmo quando o dispositivo tem `BTN_LEFT` e é reconhecido como touchpad.
- Nesse caso, `BTN_LEFT` é encaminhado como botão físico esquerdo (`btn_pending[0]`); `click_button()` não aplica a lógica de cantos.
- `TpCaps.has_right` é exposto em `lord.input.devices()` como `hasRight` e a lista das Configurações já indica botão direito físico ou clickpad.
- `tp_open()` agora consulta `EVIOCGPHYS` e `EVIOCGUNIQ` e publica `id` + `identityStable` em `lord.input.devices()`. O ID é um fingerprint FNV-1a dos dados físicos/únicos e do hardware; não usa `/dev/input/eventN`.
- Quando o kernel não fornece identidade física nem serial, o fingerprint usa bus/vendor/product/nome e `identityStable` fica falso. Esse fallback serve para diagnóstico, não deve ser usado para gravar overrides persistentes, pois dispositivos iguais podem colidir.
- `server/src/input.c` só chamava `tp_open()` depois de classificar o dispositivo como mouse ou ponteiro absoluto. Um touchpad multitouch que anuncie apenas `ABS_MT_POSITION_X/Y/SLOT`, sem `ABS_X/Y`, podia ser descartado antes do driver.
- Foi adicionada uma classificação explícita para esse conjunto de capabilities, alinhada aos critérios de `tp_open()`, e testes no `inputkind_test.c`. O teste foi compilado e executado no GitHub Actions.
- Em touchpads com botões físicos separados, pressionar BTN_LEFT/BTN_RIGHT/BTN_MIDDLE agora cancela o reconhecedor de tap ativo. Isso evita que o levantamento do dedo gere um segundo clique sintético após o clique físico.
- A API `lord.input.configureDevice(id, mode)` agora aceita `auto`, `clickpad` e `buttons`. O servidor aplica o modo somente ao dispositivo conectado com o ID informado; modos forçados são recusados quando `identityStable` é falso.
- `sdk/trackpad.js` persiste os overrides em `settings.trackpadDevices`, reaplica quando os dispositivos são reenumerados e atualiza a lista após a mudança. O `eventN` nunca é usado como chave.
- Configurações agora apresenta um seletor por touchpad. Dispositivos sem identidade física única exibem o motivo e não permitem persistência individual.
- Foram adicionados testes unitários para os três modos e para rejeitar override em identidade ambígua. O `tp-test` compilou e passou com 98/98 casos no GitHub Actions.

**Caminho do botão direito já existente**

O caminho foi seguido no código: `tp_event_at()` produz o botão direito (código interno 2), `tp_route.c` chama `TpHost.button`, `main.c` enfileira o evento e, no `SYN_REPORT`, `dispatch_pointer()` recebe o clique junto com a posição atual. Portanto, o botão direito emitido pelo driver usa o mesmo despacho do mouse. Isso confirma o caminho de entrada, mas ainda não comprova que todos os alvos da interface abrem o menu correto; essa validação continua pendente.

### 2. Menus contextuais

- Auditoria do dispatcher: `runtime/events.js` gera `onContextMenu` no `pointerup` quando o botão é `2`, usando a cadeia comum entre pressionar e soltar; `stopPropagation()` encerra o bubbling. Files (itens, cabeçalho e fundo), Dock, Lixeira e papel de parede já possuem handlers. Não foi encontrado defeito universal no dispatcher; ainda falta validar o botão emitido pelo hardware e testar cada alvo no desktop real.
- Reutilizar `sdk/contextmenu.jsx` e `sdk/layers.js`.
- [x] Adicionar menu contextual à barra de título das janelas Lord Flow: minimizar, maximizar/restaurar, mover para outro desktop e fechar.
- O menu usa o componente `Menu` existente, que é renderizado na camada popover; não cria um segundo dispatcher nem um sistema de menus paralelo.
- Apps Linux com CSD/ownFrame não recebem o menu do título desenhado pelo tema. O caminho é o protocolo xdg-shell: o app chama `xdg_toplevel.show_window_menu`; `server/src/wl/wl.c` (`top_menu`) converte a posição local em coordenadas da tela e emite o evento `menu`; o estado da janela expõe `windowMenu`, e `Window.jsx` desenha o menu nativo do shell. Isso depende de o aplicativo realmente emitir a solicitação; não se deve interceptar o clique direito dentro do conteúdo da aplicação.
- A barra de título LordOS, por outro lado, usa `onContextMenu` do runtime. `runtime/events.js` só dispara o evento secundário no `pointerup` quando o botão 2 estava na cadeia do `pointerdown`; o handler usa `stopPropagation()` para não abrir também o menu do desktop.
- Desktop/papel de parede, Files (itens, cabeçalho e fundo), Dock e Lixeira já possuem handlers; ainda falta validar em hardware e sessão real o caminho físico completo.
- `Menu.jsx` fecha por clique externo/Escape e calcula posição dentro do display; a validação visual e de foco continua pendente.

### 3. Alternador de janelas

- [x] `sdk/switchermodel.js` define `Alt+Tab` como padrão Tahoe, inclusive quando a preferência salva está ausente ou contém um valor inválido/obsoleto; o usuário continua podendo escolher `theme`, `meta`, `alt` ou `both`.
- `sdk/appswitcher.jsx` agrupa janelas por aplicativo, ordena os grupos por uso recente, permite navegar entre apps e entre janelas do app selecionado e restaura a janela escolhida.
- O switcher não abre sobre aplicativos em fullscreen, preservando o comportamento de inibição documentado.
- O App Switcher continua separado do Overview/Mission Control.
- Foi adicionada regressão em `tests/switcher_model_test.mjs` para preferência inválida cair em `alt`, mesmo em tema cujo atalho próprio é `meta`. Teste ainda precisa ser executado localmente ou confirmado em CI.

### 4. Dock e janelas minimizadas

- [x] Exibir tiles individuais das janelas minimizadas ao lado da Lixeira no Lord Flow.
- [x] Restaurar a janela exata ao clicar no tile, inclusive trocando para o desktop correspondente.
- [x] Atualizar a lista automaticamente a partir do estado de janelas: minimizar, restaurar e encerrar.
- [ ] Evoluir os tiles atuais (moldura, ícone e título) para snapshots reais do conteúdo, quando houver fonte de captura segura e barata; a implementação atual não duplica raster nem mantém cache de imagens.

### 5. Feedback de abertura de aplicativos

**Caminho auditado**

- `themes/default/components/Dock.jsx` confirma o clique com redução/opacity por 420 ms; é feedback de interação, não estado do processo. O efeito respeita Reduce Motion e limpa o timer ao desmontar.
- `sdk/dock.js` encaminha o clique sem janelas para `launchDockItem()` e `launchApp()`; quando há janelas, apenas foca/restaura a janela existente.
- `sdk/apps.js` centraliza os lançamentos: Web App, Flatpak, Linux `.desktop`, pacote `.lpk`, built-in isolado e janela nativa `wm.open()`.
- Os retornos atuais são heterogêneos: `wm.open()` devolve ID de janela; Linux/Flatpak e `launchPackage()` informam aceitação/recusa do pedido, não o primeiro frame; Web App pode lançar ou lançar exceção. Portanto, não podem ser tratados como um único sinal de "abriu".
- `sdk/windows.js` mantém o registro de janelas do shell. Uma janela nova pode servir como confirmação visual de que há uma superfície, mas não é um evento universal de processo iniciado nem cobre falhas anteriores à criação da janela.
- `server/src/app/appspawn.c` registra o processo isolado e seu broker; `server/src/app/appclient.c` cria e gerencia as janelas Wayland desse processo. O caminho examinado não expõe ao tema um evento unificado de início/erro para todas as classes de app.

**Decisão**

- Não criar indicador de carregamento com timeout arbitrário nem tratar retorno de aceitação como sucesso final.
- Próxima implementação precisa de um contrato de ciclo de vida no servidor/SDK com identificador de lançamento e estados distintos: solicitado, processo iniciado quando aplicável, primeira janela apresentada, falha e cancelamento/encerramento.
- O contrato deve cobrir Linux, Flatpak, Web Apps, pacotes isolados e apps nativos, com correlação segura entre pedido e janela. O indicador visual termina quando chega evento terminal, com timeout de segurança apenas como fallback e mensagem de estado honesta.
- Até existir esse contrato, manter o feedback de clique atual como confirmação de interação, sem alegar que o app terminou de abrir.

### 6. Abrir com e navegador

**Achados atuais**

- `apps/files/ops.js` monta os aplicativos compatíveis usando MIME do sistema (`lord.mime.guess`) e a tabela interna do Files; inclui apps Linux e Flatpak que declaram o tipo.
- O app padrão MIME do usuário/sistema é priorizado e marcado na lista; as associações são mantidas em `~/.config/mimeapps.list`, compartilhadas com `xdg-open` e apps Linux.
- O menu contextual de arquivos já oferece `Abrir` e submenu `Abrir com`; esse submenu lista os handlers disponíveis, inclusive provedores externos.
- Duplo clique usa `openEntry()`: abre o handler padrão automaticamente quando existe; se não há app instalado, pode oferecer provedor compatível ou Quick Look. Não há seletor obrigatório em todos os duplos cliques.
- A escolha de app padrão por categoria está em Configurações → Apps padrão (`apps/settings/DefaultApps.jsx`), com navegador, e-mail, música, vídeo, imagens, PDF, texto, terminal e calendário. A configuração é freedesktop, não uma base paralela.
- Para provedores externos, Files permite memorizar "Sempre abrir arquivos .ext com…". Para apps Linux/nativos, a escolha permanente é feita pelas associações MIME em Apps padrão; o submenu Abrir com executa uma escolha pontual.
- `sdk/sysopen.js` encaminha URLs HTTP/HTTPS ao handler padrão. O portal valida pedidos de apps Linux e pede confirmação quando o app solicitante está em segundo plano.

**Conclusão**

- O seletor e as associações já existem; o problema a resolver é de descoberta/clareza da experiência, não ausência total da função.
- Melhorias Tahoe devem deixar visível a opção de escolha pontual e o caminho para definir padrão, sem substituir `mimeapps.list` nem criar outra associação de arquivos.
- **Lacuna de UX confirmada:** para handlers MIME comuns, não há preferência "Perguntar sempre" por tipo. O duplo clique abre o padrão automaticamente; o menu contextual permite escolher pontualmente; Apps padrão permite fixar uma associação. A opção "Ask Each Time" documentada em Files aplica-se aos provedores externos por extensão, não aos handlers MIME freedesktop.
- Evolução proposta: acrescentar preferência opcional "Perguntar sempre" por MIME/tipo, com diálogo que ofereça abrir uma vez ou definir como padrão. O modo automático continua sendo o padrão inicial e a associação permanente continua em `mimeapps.list`.
- Revisar a apresentação de navegadores candidatos e uma opção "Perguntar sempre" sem alterar a política de segurança do portal. Para links HTTP/HTTPS solicitados por apps Linux, manter a confirmação existente quando o solicitante está em segundo plano.

## Estado desta rodada

- [x] Branch de trabalho criada: `tahoe/round-1-functional-fixes`.
- [x] Auditoria do código de detecção do clickpad e da lógica de clique físico.
- [x] Identificada lacuna de cobertura dos testes para clique físico nos cantos.
- [x] Adicionar gestos simulados para clique físico no canto direito, canto esquerdo e centro.
- [x] Adicionar testes para mapeamento de botão direito nos dois cantos, centro como botão esquerdo e preservação do modo de dois dedos.
- [x] Adicionar teste de regressão para touchpad com botões físicos separados.
- [x] Fortalecer a regressão: o dispositivo simulado anuncia explicitamente BTN_RIGHT, evitando que o teste represente por engano um touchpad sem botão direito físico.
- [x] Corrigir a triagem inicial para aceitar touchpads multitouch sem eixos legados `ABS_X/Y`, mantendo o critério de capabilities do driver.
- [x] Adicionar regressões para touchpad multitouch com `INPUT_PROP_POINTER`, `BTN_LEFT`, `INPUT_PROP_BUTTONPAD` e touchscreen `INPUT_PROP_DIRECT`.
- [x] Expor fingerprint de dispositivo baseado em `EVIOCGPHYS`/`EVIOCGUNIQ`, com indicação explícita quando a identidade é apenas aproximada.
- [x] Implementar API de override individual (Auto / Clickpad / Botões separados), com bloqueio para identidade ambígua.
- [x] Persistir preferências individuais e reaplicar na reenumeração.
- [x] Expor controles individuais em Configurações e adicionar testes unitários de modo e segurança.
- [ ] Corrigir e testar menus contextuais no caminho completo.
- A auditoria confirmou que `runtime/events.js` só dispara `onContextMenu` para o botão 2 no `pointerup`, e o evento percorre os ancestrais comuns entre pressionar e soltar; `stopPropagation()` interrompe o restante do caminho. Files (itens, cabeçalho e fundo), Dock, Lixeira e papel de parede já possuem handlers. A inspeção não encontrou um defeito universal no dispatcher; faltam validação do botão emitido pelo hardware e teste funcional em cada alvo.
- [ ] Implementar as demais melhorias da Rodada 1.
- [ ] Executar build e testes no ambiente de desenvolvimento (ainda não executados: o ambiente desta sessão não conseguiu acessar GitHub para clonar o repositório).
- [ ] Validar em hardware físico.

**Importante:** `tp-test` (98/98), `inputkind-test` e `iconfind-test` passaram no GitHub Actions, e o instalador passou em `bash -n`. Isso valida os testes automatizados em Linux, mas não substitui a validação do touchpad no notebook real nem a suíte completa/build integral do LordOS. A correção do touchpad permanece pendente até a execução da suíte e o teste no hardware.

### 3. Alt + Tab

- `sdk/switchermodel.js` agora define `alt` como padrão do alternador de aplicativos. A configuração continua permitindo `theme`, `meta`, `alt` e `both`; uma preferência já salva pelo usuário continua sendo respeitada.
- O SDK gera o perfil `flow+tab-alt`, reconhecido pelo `server/src/wl/keygrab.c`, que captura Alt+Tab como combinação de retenção e entrega a navegação ao App Switcher até soltar Alt. O atalho Meta+Tab deixa de ser o padrão, mas continua selecionável nas Configurações.
- Adicionado teste de modelo para o padrão Alt e para o perfil de captura. Ainda precisa executar os testes no ambiente de build e confirmar em sessão Wayland real.

### 7. Iconografia e integração MacTahoe

- [x] Fixar uma revisão upstream do [MacTahoe-icon-theme](https://github.com/vinceliuice/MacTahoe-icon-theme) e adicionar instalador reproduzível para o sistema/imagem.
- [x] Instalar variantes azul, clara e escura preservando COPYING e AUTHORS do projeto original.
- [x] Fazer o resolvedor C priorizar a variante MacTahoe ativa (dark/light), procurar variantes do usuário e do sistema, manter overrides hicolor e aceitar `apps/32`, `apps/scalable` e `status/symbolic` além do formato hicolor.
- [x] Expor as variantes MacTahoe dark/light como tema de ícones GTK por settings.ini, GSettings keyfile e Settings portal, acompanhando a aparência do sistema.
- [x] Adicionar regressões de ordem de resolução para PNG, SVG e symbolic MacTahoe, sem quebrar os caminhos hicolor e Adwaita.
- [x] Integrar o instalador ao `tools/iso/mkdeb.sh`; o pacote `.deb` inclui os temas e cursores e o ISO os recebe ao instalar o pacote.
- [ ] Criar a família de ícones nativos Tahoe para os aplicativos LordOS; os apps nativos continuam usando componentes React próprios, não os ícones genéricos do tema upstream.
- [x] Compilar e executar `iconfind-test` no GitHub Actions; validar a sintaxe do instalador com `bash -n`.
- [ ] Adicionar teste automatizado específico para a troca de ícone no Settings portal; compilar e executar a suíte completa do servidor.

A implementação e o guia estão em `packaging/icons/` e `docs/ICONS.md`. A revisão upstream está fixada em `839848b9a8a38a92a6936e30c4abe35cc6f2546d`; o repositório original informa licença GPL-3.0. CI do commit `bd1be104e1bcad8086a61dd0dc12d99bce03b7bf`: `iconfind-test` passou, `tp-test` passou com 98/98, `inputkind-test` passou e `bash -n packaging/icons/install-mactahoe.sh` passou. A suíte completa do servidor, os testes do portal e a validação em hardware ainda não foram executados.


## Rodada 2 — Fundação do Tahoe Design System

- [x] Criar `themes/tahoe/` como tema independente, sem substituir Lord Flow.
- [x] Registrar o tema no catálogo de aparências e documentar em `themes/README.md`.
- [x] Criar tokens próprios para superfícies claras/escuras, materiais translúcidos, blur adaptável, sombras, bordas e acentos.
- [x] Definir geometria compartilhada para controles, grupos e menus pelo contrato de tokens do `ThemeProvider`.
- [x] Criar Menu Bar Tahoe própria (32 px, material translúcido, destaque arredondado, borda e sombra sutis).
- [x] Criar moldura de janela Tahoe própria (barra de título de 46 px, cantos de 16 px e sombras ativas/inativas por token).
- [x] Criar Dock Tahoe próprio com material de vidro e sombra elevada, mantendo magnificação, reordenação, miniaturas e menus do modelo original.
- [x] Definir `Alt+Tab` na tabela de atalhos do shell Tahoe.
- [x] Reutilizar os componentes nativos atuais de Lord Flow nesta primeira fatia, preservando a lógica do Dock, janelas, menus, Control Center, notificações, widgets e multi-monitor.
- [ ] Separar progressivamente os componentes visuais Tahoe próprios restantes: Control Center, Notification Center, Launchpad e menus.
- [ ] Adicionar arte original de ícones, wallpapers e sons Tahoe.
- [ ] Executar `npm run build`, selecionar Tahoe em Configurações → Aparência e validar visualmente em hardware e em múltiplos monitores.
- [ ] Incluir o tema no manifesto de código verificado/assinado da versão de lançamento.

A especificação da fundação está em `docs/TAHOE-DESIGN-SYSTEM.md`. A criação dos arquivos é implementação de código-fonte; build, seleção em sessão e testes visuais ainda não foram executados nesta etapa.
