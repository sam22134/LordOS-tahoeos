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
- A identidade estável do dispositivo ainda não é exposta; `tp_open()` coleta bus/vendor/product e nome, mas não consulta `EVIOCGPHYS` nem `EVIOCGUNIQ`.
- `server/src/input.c` só chamava `tp_open()` depois de classificar o dispositivo como mouse ou ponteiro absoluto. Um touchpad multitouch que anuncie apenas `ABS_MT_POSITION_X/Y/SLOT`, sem `ABS_X/Y`, podia ser descartado antes do driver.
- Foi adicionada uma classificação explícita para esse conjunto de capabilities, alinhada aos critérios de `tp_open()`, e testes no `inputkind_test.c`. A suíte ainda precisa ser executada.
- Próximo passo de UX: expor capabilities sem inferência arriscada e implementar override por dispositivo (Auto / Clickpad / Botões separados), persistido por identidade estável do hardware. Não usar override global.
- O override ainda não foi implementado; `hasRight` será apenas diagnóstico.

### 2. Menus contextuais

- Reutilizar `sdk/contextmenu.jsx` e `sdk/layers.js`.
- Garantir botão direito no desktop, arquivos, Dock, lixeira e janelas.
- Validar que o evento secundário emitido pelo touchpad percorre o mesmo caminho do mouse.
- Fechar menus por clique externo e Escape; respeitar limites da tela e foco.

### 3. Alternador de janelas

- Definir Alt+Tab como padrão do tema Tahoe.
- Preservar configuração do usuário e inibição apropriada em fullscreen.
- Manter App Switcher (alternância rápida) separado do Mission Control (visão geral).

### 4. Dock e janelas minimizadas

- Exibir miniaturas individuais das janelas minimizadas ao lado da Lixeira.
- Restaurar a janela exata ao clicar na miniatura.
- Atualizar miniaturas ao minimizar, restaurar e encerrar.
- Limitar memória/cache e respeitar Reduce Motion e perfil econômico.

### 5. Feedback de abertura de aplicativos

- Estados: solicitação, iniciando, janela pronta e falha.
- Usar eventos reais do ciclo de vida; não manter animação infinita nem indicar sucesso antes da janela abrir.
- Respeitar Reduce Motion.

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
- [ ] Fechar estratégia de detecção adicional com base em capabilities reais.
- [ ] Corrigir e testar menus contextuais no caminho completo.
- [ ] Implementar as demais melhorias da Rodada 1.
- [ ] Executar build e testes no ambiente de desenvolvimento (ainda não executados: o ambiente desta sessão não conseguiu acessar GitHub para clonar o repositório).
- [ ] Validar em hardware físico.

**Importante:** os casos de teste foram escritos, mas ainda não foram compilados nem executados. A inspeção do código e os testes simulados não substituem a validação no notebook real. A correção do touchpad permanece pendente até a execução da suíte e o teste no hardware.
