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
- [ ] Implementar testes automatizados para canto direito, canto esquerdo e centro.
- [ ] Fechar estratégia de detecção adicional com base em capabilities reais.
- [ ] Corrigir e testar menus contextuais no caminho completo.
- [ ] Implementar as demais melhorias da Rodada 1.
- [ ] Executar build e testes no ambiente de desenvolvimento.
- [ ] Validar em hardware físico.

**Importante:** a inspeção do código e os testes simulados não substituem a validação no notebook real. Até a execução dos testes, a correção do touchpad permanece pendente.
