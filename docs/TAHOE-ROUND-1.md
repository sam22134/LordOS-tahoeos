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
- Apps Linux com CSD/ownFrame continuam usando `win.windowMenu`, alimentado pelo caminho próprio do compositor.
- Desktop/papel de parede, Files (itens, cabeçalho e fundo), Dock e Lixeira já possuem handlers; ainda falta validar em hardware e sessão real o caminho físico completo.
- Fechar menus por clique externo e Escape; respeitar limites da tela e foco.

### 3. Alternador de janelas

- Definir Alt+Tab como padrão do tema Tahoe.
- Preservar configuração do usuário e inibição apropriada em fullscreen.
- Manter App Switcher (alternância rápida) separado do Mission Control (visão geral).

### 4. Dock e janelas minimizadas

- [x] Exibir tiles individuais das janelas minimizadas ao lado da Lixeira no Lord Flow.
- [x] Restaurar a janela exata ao clicar no tile, inclusive trocando para o desktop correspondente.
- [x] Atualizar a lista automaticamente a partir do estado de janelas: minimizar, restaurar e encerrar.
- [ ] Evoluir os tiles atuais (moldura, ícone e título) para snapshots reais do conteúdo, quando houver fonte de captura segura e barata; a implementação atual não duplica raster nem mantém cache de imagens.

### 5. Feedback de abertura de aplicativos

- O Dock agora dá feedback imediato e curto ao clique (ícone reduz e retorna em 420 ms), respeitando Reduce Motion. É confirmação da interação, não indica que o aplicativo terminou de abrir.
- Próxima evolução: estados reais de solicitação, processo iniciado, primeira janela exibida e falha. Não manter animação infinita nem indicar sucesso antes da janela abrir.

### 6. Abrir com e navegador

- Revisar a experiência já existente de Open With e Default Apps.
- Oferecer escolha pontual e preferência permanente por MIME quando aplicável.
- Não criar mecanismo paralelo às associações MIME/freedesktop.

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
