# MASTER PROMPT — NINTENDO DS STRATEGY TYCOON / INCREMENTAL WAR GAME

## 1. MISSÃO DO PROJETO

Você é uma equipe multidisciplinar de desenvolvimento de jogos composta por Game Designers, Gameplay Programmers, Engine Programmers, Technical Artists, Pixel Artists, UI/UX Designers, Sound Designers e especialistas em otimização para hardware limitado.

Sua missão é conceber e desenvolver um jogo original para **Nintendo DS**, combinando estratégia militar, Tycoon, incremental progression, gerenciamento de império e simulação automática de batalhas em tempo real.

O resultado precisa ser um jogo completo, divertido, visualmente impressionante dentro das possibilidades do hardware original e com alto potencial de retenção.

O jogador deverá sentir uma vontade constante de continuar jogando, melhorar seu exército, expandir seu império, desbloquear tecnologias, evoluir suas tropas e enfrentar inimigos progressivamente mais perigosos.

**A qualidade visual é um requisito central e inegociável do projeto.** Não entregue um jogo funcional com apresentação visual genérica, sprites improvisados ou interfaces rudimentares.

O objetivo é desenvolver um jogo que pareça um título comercial cuidadosamente produzido para o Nintendo DS.

## 2. VISÃO DO PRODUTO

O jogador assume o papel de um grande general da Antiguidade, responsável por construir, administrar e comandar um império militar.

A experiência combina quatro pilares.

1. **Military Tycoon:** construir e administrar uma cidade militar, desenvolver infraestrutura, administrar recursos e expandir o poder econômico.
2. **Army Management:** recrutar, treinar, equipar, especializar e organizar soldados em divisões.
3. **Automatic Battle Simulator:** assistir a batalhas em tempo real, com movimentação, formações, combate, flanqueamento, baixas e habilidades especiais.
4. **Incremental Progression:** evoluir continuamente, desbloquear conteúdo, conquistar recompensas, derrotar entidades mitológicas e desenvolver um exército cada vez mais poderoso.

O jogador não controla individualmente cada soldado durante as batalhas. Ele prepara o exército, define suas formações, organiza suas divisões e acompanha a simulação. Durante o combate, pode ativar habilidades especiais do general.

A experiência deve ser satisfatória tanto para quem gosta de observar grandes batalhas quanto para quem gosta de otimizar sistemas de gerenciamento.

## 3. PLATAFORMA E RESTRIÇÕES TÉCNICAS

O destino é o Nintendo DS original.

Considere rigorosamente as limitações reais do hardware.

- Resolução nativa de 256 × 192 pixels por tela.
- Duas telas simultâneas.
- Touchscreen resistivo.
- Capacidade limitada de processamento.
- Memória RAM extremamente limitada.
- Restrições de armazenamento, carregamento e quantidade de recursos gráficos.
- Necessidade de execução estável em hardware real.

Você tem autonomia para escolher a linguagem, bibliotecas, ferramentas, engine, arquitetura, técnicas de renderização e estratégias de otimização.

Investigue as ferramentas e possibilidades adequadas ao desenvolvimento nativo para Nintendo DS. Avalie alternativas como devkitPro/libnds quando pertinente, sem assumir antecipadamente que uma tecnologia específica é obrigatória.

**Não desenvolva um jogo web, uma simulação de desktop ou um protótipo que apenas imite a aparência de um Nintendo DS.** O produto final precisa ser executável como jogo para a plataforma pretendida.

Antes de implementar sistemas complexos, estabeleça orçamentos mensuráveis de memória, processamento, sprites, animações, efeitos e armazenamento.

O jogo deve ser projetado para funcionar no hardware real. Emuladores podem ser utilizados durante o desenvolvimento, mas não substituem validação de compatibilidade e desempenho no console quando houver acesso ao equipamento.

### Escalabilidade

O jogador deverá conseguir desenvolver exércitos progressivamente maiores, chegando eventualmente a centenas ou milhares de soldados em seu império.

A representação e a simulação devem ser escaláveis.

Investigue soluções como entidades otimizadas, simulação por divisões, atualização em diferentes frequências, sprites agrupados, técnicas de animação eficientes e outras estratégias adequadas ao hardware.

Não estabeleça limites arbitrários sem investigar alternativas.

Também não prometa milhares de soldados renderizados individualmente e com simulação completa se os testes demonstrarem que isso é inviável.

Qualquer compromisso técnico deverá preservar a identidade do jogo, a sensação de escala e a qualidade visual.

## 4. DIREÇÃO ARTÍSTICA

### Identidade visual

A estética deve combinar:

- Pixel art de alta qualidade.
- Antiguidade histórica.
- Dark fantasy.
- Mitologia antiga.
- Cenários épicos.
- Arquitetura militar monumental.
- Personagens e criaturas com silhuetas marcantes.

A referência artística é uma combinação de jogos 2D de estratégia e RPG para consoles portáteis, com acabamento profissional e personalidade visual própria.

Não copie diretamente outros jogos.

### Hierarquia visual

Utilize diferentes níveis de detalhamento conforme a importância dos elementos.

- Soldados comuns devem possuir sprites compactos, reconhecíveis e visualmente consistentes.
- Unidades de elite devem ter equipamentos, silhuetas e detalhes distintos.
- Cavalaria e máquinas de cerco precisam ser facilmente identificáveis.
- Generais devem ter presença visual diferenciada.
- Chefes mitológicos devem possuir sprites únicos, detalhados e impressionantes.
- Cenários devem apresentar variação ambiental e elementos de profundidade.

### Animações e efeitos

O combate precisa ser agradável de assistir.

Implemente, dentro dos limites do hardware:

- Ciclos de caminhada e corrida.
- Ataques e defesas.
- Recuos e impactos.
- Animações de morte.
- Cavalaria em movimento.
- Projéteis e flechas.
- Poeira, fumaça e fogo.
- Explosões e efeitos de habilidades.
- Efeitos ambientais.
- Animações de chefes.
- Feedback visual para dano, moral e acontecimentos importantes.

Evite animações excessivamente repetitivas e movimentos que façam os soldados parecerem objetos deslizando pelo cenário.

### Interface

A interface deve ser bonita, legível e consistente.

Não utilize menus genéricos ou layouts que pareçam ferramentas de desenvolvimento.

Crie uma identidade visual própria, com tipografia pixelada legível, ícones coerentes, painéis militares, indicadores e animações discretas.

Todo elemento deve respeitar a resolução nativa do DS.

**A otimização técnica não deve ser usada como justificativa para negligenciar a direção artística.**

## 5. CIVILIZAÇÕES

O jogador poderá escolher sua civilização no início da campanha.

Considere civilizações como:

- Gregos.
- Romanos.
- Cartagineses.
- Assírios.
- Sumérios.
- Chineses antigos.
- Outras civilizações historicamente inspiradas.

Cada civilização deverá apresentar identidade própria.

Isso inclui unidades, equipamentos, arquitetura, cores, formações, especializações, tecnologias e características militares.

Evite criar civilizações que sejam apenas versões recoloridas umas das outras.

O jogador também poderá incorporar tropas estrangeiras por meio de conquistas, mercenários, tecnologias e outros sistemas de progressão.

A combinação de unidades de diferentes civilizações deverá abrir possibilidades estratégicas.

## 6. SISTEMA DE CAMPANHA INFINITA

O jogo terá um modo principal de campanha infinita.

O jogador participa de uma sequência contínua de conflitos militares, desenvolve seu império e enfrenta ameaças progressivamente mais complexas.

### Expansão territorial

O império começa pequeno.

Sua expansão será gradual, conquistando territórios, estabelecendo posições militares e ampliando sua influência.

O mapa de campanha deverá apresentar regiões, territórios, civilizações rivais e locais de interesse.

Entretanto, a expansão territorial não deve se tornar o elemento dominante da experiência.

O foco principal permanece no gerenciamento militar e na evolução do exército.

### Defesa do império

O império estará constantemente sujeito a ameaças.

Implemente eventos que justifiquem a continuidade das batalhas, como:

- Invasões de civilizações rivais.
- Ataques de exércitos estrangeiros.
- Rebeliões.
- Tentativas de reconquista.
- Cercos a fortalezas.
- Ataques de criaturas mitológicas.
- Emboscadas.
- Eventos militares especiais.
- Ameaças relacionadas aos deuses.

O jogador deverá precisar defender suas conquistas.

Isso cria uma justificativa narrativa e mecânica para a campanha interminável.

As ameaças devem variar em intensidade e frequência, oferecendo períodos de preparação e desenvolvimento.

### Eventos e variedade

Evite uma sequência monótona de batalhas idênticas.

Alterne entre confrontos comuns, invasões, defesa de território, cercos, emboscadas, objetivos especiais e batalhas contra chefes.

A campanha deve permanecer interessante mesmo depois de muitas horas.

## 7. SISTEMA DE BATALHAS

### Estrutura

As batalhas acontecem automaticamente em tempo real.

O jogador prepara o exército antes do combate e acompanha sua execução.

A duração-base desejada para batalhas comuns é de aproximadamente 2 a 5 minutos em velocidade normal.

O jogador poderá escolher entre assistir à batalha completa ou acelerar sua execução.

A aceleração deverá alterar a velocidade da simulação, sem modificar artificialmente os resultados estratégicos.

Implemente controles de velocidade adequados à plataforma.

### Mecânicas

As batalhas deverão considerar:

- Composição dos exércitos.
- Quantidade de soldados.
- Qualidade das unidades.
- Equipamentos.
- Níveis e especializações.
- Formação.
- Moral.
- Disciplina.
- Veterania.
- Terreno.
- Flanqueamento.
- Ataques pela retaguarda.
- Resistências e fraquezas.
- Vantagens de determinadas unidades.
- Habilidades do general.
- Condições especiais do campo de batalha.

As decisões tomadas durante a preparação precisam influenciar efetivamente os resultados.

Não reduza o combate a uma comparação simples de números.

### Formações

O jogador poderá selecionar uma formação geral e organizar individualmente suas divisões dentro dela.

Exemplos:

- Falange.
- Linha de infantaria.
- Formação defensiva.
- Formação em cunha.
- Formação circular.
- Formação de cavalaria.
- Formação de arqueiros protegidos.

Cada formação deverá ter vantagens e vulnerabilidades reais.

Uma falange, por exemplo, deve ser poderosa frontalmente e vulnerável a ataques laterais e pela retaguarda.

### Controle durante a batalha

O jogador poderá ativar habilidades especiais do general.

Não haverá microgerenciamento individual dos soldados.

As habilidades devem possuir limitações apropriadas, como cooldowns, custos ou condições de ativação.

### Terrenos e objetivos

Inclua diferentes campos de batalha.

- Planícies.
- Florestas.
- Montanhas.
- Desertos.
- Rios.
- Fortalezas.
- Campos de cerco.
- Arenas mitológicas.

Terreno e objetivos devem influenciar a estratégia.

### Apresentação

A batalha deve ser visualmente espetacular.

A tela superior deverá priorizar a visualização do campo de batalha.

A tela inferior deverá funcionar como painel de comando e informações militares.

O jogador poderá consultar:

- Quantidade de soldados vivos.
- Baixas.
- Moral.
- Integridade das divisões.
- Situação dos flancos.
- Eficiência das formações.
- Habilidades disponíveis.
- Situação dos objetivos.
- Informações relevantes sobre o inimigo.

Apresente alertas contextuais para acontecimentos críticos.

Exemplos:

- Uma divisão entrou em colapso.
- O flanco esquerdo foi rompido.
- Um general foi abatido.
- A cavalaria inimiga iniciou uma investida.
- Um chefe ativou uma habilidade especial.

Não sobrecarregue a interface com informações simultâneas desnecessárias.

## 8. CHEFES MITOLÓGICOS

Os chefes mitológicos serão alguns dos momentos mais importantes do jogo.

A cada dez batalhas, deverá surgir uma oportunidade de enfrentar um grande chefe mitológico, com preparação adequada.

Exemplos de entidades incluem Zeus, Ares, Poseidon, Hades, Cronos e outras figuras de diferentes tradições mitológicas.

Cada chefe deverá apresentar identidade própria.

Não force todos os deuses a serem gigantes humanoides.

Algumas entidades poderão ser representadas por manifestações cósmicas, fenômenos ambientais, criaturas, entidades simbólicas ou outras interpretações coerentes com sua mitologia.

### Requisitos dos chefes

Cada chefe deverá possuir:

- Apresentação visual exclusiva.
- Introdução especial.
- Trilha sonora própria ou variação temática.
- Habilidades características.
- Resistências e vulnerabilidades.
- Múltiplas fases quando apropriado.
- Mudanças ambientais.
- Comportamento de combate próprio.
- Recompensas especiais.

Exemplos de conceitos:

- Zeus altera o campo de batalha com tempestades e raios.
- Ares influencia a agressividade e a fúria dos combatentes.
- Poseidon utiliza forças marítimas e inundações.
- Hades pode invocar forças espectrais.
- Cronos pode manipular o ambiente e o tempo.

Esses exemplos são pontos de partida, não uma lista fechada.

### Dificuldade

As batalhas contra deuses devem ser mais longas, épicas e exigentes.

Elas devem representar acontecimentos especiais.

O jogador deverá ter tempo para se preparar e compreender as ameaças.

A dificuldade deverá considerar o desenvolvimento real do jogador, sua composição militar e sua preparação.

Não permita que um chefe se torne trivial apenas por progressão numérica.

Também não crie chefes artificialmente impossíveis.

### Recompensas divinas

Derrotar um deus deverá conceder recompensas permanentes.

Exemplos:

- Relíquias.
- Bônus permanentes.
- Tecnologias especiais.
- Habilidades divinas.
- Melhorias militares.
- Poderes utilizáveis em batalhas futuras.

O jogador terá espaços limitados para equipar poderes divinos, incentivando escolhas estratégicas.

## 9. SOLDADOS E DIVISÕES

Utilize um sistema híbrido de gerenciamento individual e coletivo.

As divisões serão a principal unidade de organização do exército.

Entretanto, os soldados que compõem essas divisões terão características individuais.

### Soldado individual

Cada soldado poderá possuir:

- Identidade ou nome.
- Nível.
- Experiência.
- Atributos.
- Histórico militar.
- Ferimentos.
- Veterania.
- Especializações quando aplicável.

Soldados extraordinários poderão se tornar veteranos lendários, recebendo reconhecimento especial.

### Ferimentos e mortalidade

Soldados poderão:

- Morrer permanentemente.
- Ser capturados.
- Desertar.
- Sofrer ferimentos.
- Sofrer sequelas permanentes.

Ferimentos graves poderão incluir perda de membros e outras incapacidades.

Essas sequelas deverão ter consequências mecânicas significativas.

O jogador poderá avaliar a recuperação, reassociação a funções alternativas, aposentadoria ou permanência do soldado no serviço com penalidades.

### Divisões

Cada divisão deverá possuir:

- Identidade.
- Composição.
- Nível de treinamento.
- Disciplina.
- Moral.
- Coesão.
- Veterania coletiva.
- Histórico de batalhas.

Uma divisão poderá sobreviver a uma batalha mesmo perdendo parte de seus integrantes.

A perda de soldados veteranos deverá ter peso estratégico e emocional.

## 10. UNIDADES E EVOLUÇÃO

Comece com um catálogo relativamente pequeno de unidades.

A profundidade de progressão é mais importante que uma quantidade enorme de unidades superficiais.

Considere categorias como:

- Infantaria leve.
- Infantaria pesada.
- Lanceiros.
- Arqueiros.
- Cavalaria leve.
- Cavalaria pesada.
- Unidades especiais.
- Máquinas de cerco.

Cada unidade precisa ter uma função estratégica clara.

### Evolução em camadas

Implemente três dimensões de progressão.

**Nível individual**

Soldados acumulam experiência e desenvolvem atributos.

**Evolução de classe**

Uma unidade poderá evoluir para versões mais avançadas.

Exemplo:

Recruta lanceiro → Lanceiro veterano → Hoplita → Hoplita de elite.

**Especializações**

Unidades poderão seguir caminhos distintos, como ataque, defesa, mobilidade ou outras especializações coerentes.

Treinamento, equipamentos e veterania também deverão influenciar a qualidade militar.

A progressão deverá ser profunda, com possibilidades de desenvolvimento de longo prazo.

## 11. ECONOMIA E SISTEMA TYCOON

O jogador administra uma economia militar que sustenta o crescimento do império.

A economia deverá começar simples e desbloquear novas camadas de complexidade conforme a campanha avança.

Considere recursos como:

- Ouro.
- Alimentos e suprimentos.
- Materiais.
- Conhecimento.
- Recursos militares específicos.

Os recursos devem ter finalidades claras.

### Produção e manutenção

Construções produzem recursos enquanto o jogo está ativo.

A manutenção do exército consome recursos.

Vitórias podem conceder recompensas importantes.

Conquistas territoriais podem desbloquear novas fontes de receita.

Pesquisas e melhorias aumentam a eficiência produtiva.

**Não implemente progressão offline.** Recursos, treinamento, produção e melhorias devem avançar enquanto o jogo estiver efetivamente em execução.

### Balanceamento econômico

A economia não pode se tornar frustrante.

Evite espirais de dívida irreversíveis.

Uma derrota não pode tornar impossível continuar jogando.

A manutenção não deverá causar falência inevitável.

O jogador precisa ter caminhos razoáveis de recuperação.

Evite grind excessivo e custos que anulam artificialmente o progresso.

## 12. CIDADE MILITAR E CONSTRUÇÕES

O jogador começará com um pequeno acampamento militar.

Com o tempo, desenvolverá uma cidade militar de grande porte.

A evolução deverá ocorrer em três dimensões.

1. Construções individuais sobem de nível.
2. O território urbano se expande.
3. A arquitetura e a identidade visual da cidade mudam conforme as eras.

Considere construções como:

- Quartel.
- Academia militar.
- Arsenal.
- Estábulos.
- Hospital.
- Oficina.
- Biblioteca.
- Templo.
- Centro de comando.

Cada construção deverá ter utilidade mecânica.

Os edifícios poderão desbloquear unidades, melhorias, tecnologias, recursos e especializações.

A cidade deverá apresentar mudanças visuais perceptíveis conforme evolui.

O jogador precisa olhar para sua base e reconhecer o resultado de seu desenvolvimento.

## 13. SISTEMA DE ERAS E PRESTÍGIO

Implemente progressão por eras históricas e um sistema opcional de prestígio.

### Eras

A evolução entre eras será híbrida.

Considere:

- Pesquisas.
- Tecnologias.
- Conquistas militares.
- Marcos da campanha.
- Desenvolvimento da infraestrutura.
- Grandes vitórias.

Cada era deverá desbloquear novas possibilidades militares e econômicas.

A transição deverá produzir mudanças visuais na cidade, equipamentos e unidades.

### Prestígio

O jogador poderá voluntariamente reiniciar parte da progressão para receber bônus permanentes.

Esse sistema deverá ter vantagens claras e preservar recompensas importantes.

O prestígio precisa ser uma escolha estratégica, nunca uma obrigação para continuar avançando.

## 14. GENERAL DO JOGADOR

O general deverá evoluir junto com o império.

Implemente um sistema de progressão completo.

Considere:

- Níveis.
- Atributos.
- Árvore de habilidades.
- Especializações.
- Equipamentos.
- Armaduras.
- Armas.
- Títulos.
- Reconhecimento.
- Poderes divinos.

As habilidades poderão se concentrar em liderança, estratégia, logística, combate e outras áreas relevantes.

O general deverá ser uma representação visível da trajetória do jogador.

## 15. PROGRESSÃO E RECOMPENSAS

A progressão deverá ser um dos maiores pontos fortes do jogo.

O jogador precisa sentir satisfação frequente e vontade de continuar evoluindo.

Implemente recompensas em diferentes escalas.

### Recompensas frequentes

- Recursos.
- Experiência.
- Melhorias.
- Treinamentos.
- Equipamentos.
- Desbloqueios menores.

### Recompensas intermediárias

- Novas unidades.
- Evoluções de classe.
- Tecnologias.
- Construções.
- Especializações.
- Novas regiões.

### Grandes recompensas

- Chefes derrotados.
- Relíquias divinas.
- Poderes permanentes.
- Novas eras.
- Títulos.
- Unidades lendárias.
- Grandes conquistas militares.

A progressão precisa ser visualmente perceptível.

Evite melhorias que apenas alteram números sem produzir qualquer diferença perceptível na experiência.

## 16. DERROTAS E DIFICULDADE

A filosofia de balanceamento é **Challenging, Never Frustrating**.

Derrotas devem ter consequências relevantes.

Entretanto, o jogador nunca poderá ficar permanentemente impossibilitado de continuar a campanha.

Considere:

- Perda de soldados.
- Destruição de divisões.
- Captura e deserção.
- Perda de equipamentos.
- Gastos de recuperação.
- Perda temporária de territórios.
- Crises econômicas controladas.

Derrotas contra chefes poderão ter penalidades superiores.

Sempre deverá existir uma rota razoável de recuperação.

### Dificuldade adaptativa

A dificuldade deve considerar o poder real do jogador.

Considere quantidade e qualidade das tropas, composição, equipamentos, tecnologia, veterania e preparação.

Evite escalonamento artificial que faça os inimigos ficarem indefinidamente tão fortes quanto o jogador.

O jogador deve sentir que está ficando mais poderoso.

Chefes e eventos especiais precisam apresentar desafios reais, mas justos.

### Métricas de balanceamento

Crie métricas internas para avaliar:

- Taxa de vitória.
- Quantidade média de baixas.
- Tempo médio de recuperação.
- Progressão econômica.
- Tempo para desbloqueios.
- Frequência de derrotas.
- Evolução do poder militar.

Utilize essas informações para ajustar o balanceamento durante o desenvolvimento.

## 17. EXPERIÊNCIA NAS DUAS TELAS

A interface deverá aproveitar as características do Nintendo DS.

### Tela superior

Durante batalhas, priorizar o campo de batalha e a apresentação visual.

Durante gerenciamento, mostrar a visão principal da cidade, exército ou informações relevantes.

### Tela inferior

Durante batalhas, funcionar como painel de comando militar e consulta de informações.

Durante gerenciamento, concentrar interações, menus, recrutamento, pesquisas, construções e organização das tropas.

A tela inferior deverá utilizar o touchscreen de forma funcional.

As interfaces poderão mudar de acordo com o contexto.

Priorize legibilidade e navegação rápida.

## 18. ÁUDIO E APRESENTAÇÃO

A identidade sonora deverá reforçar a escala e a atmosfera.

Considere:

- Música militar e épica.
- Temas específicos para civilizações.
- Temas de chefes.
- Sons de combate.
- Cavalaria.
- Máquinas de cerco.
- Alertas militares.
- Construções.
- Recompensas.
- Evoluções.
- Eventos especiais.

As batalhas contra deuses deverão receber tratamento audiovisual diferenciado.

A apresentação de grandes conquistas deverá gerar satisfação perceptível.

## 19. METODOLOGIA DE DESENVOLVIMENTO

O desenvolvimento deverá ser modular.

Entretanto, a arquitetura e o planejamento precisam considerar o jogo completo desde o início.

Não desenvolva todos os sistemas simultaneamente sem validação.

### Fase 1 — Inspeção e planejamento

Antes de escrever código:

1. Inspecione completamente o repositório existente.
2. Identifique ferramentas, arquivos, dependências e estado atual do projeto.
3. Verifique se existe implementação anterior.
4. Identifique limitações técnicas relevantes.
5. Pesquise e selecione a stack adequada ao Nintendo DS.
6. Defina arquitetura e organização dos módulos.
7. Defina orçamento inicial de memória e processamento.
8. Crie um plano de desenvolvimento com marcos verificáveis.

Documente as decisões técnicas e suas justificativas.

### Fase 2 — Protótipo técnico

Implemente um protótipo mínimo que valide:

- Inicialização no Nintendo DS.
- Uso das duas telas.
- Entrada touchscreen.
- Renderização de sprites.
- Animações.
- Áudio.
- Salvamento.
- Desempenho básico.

Valide a compatibilidade com o ambiente de execução escolhido.

### Fase 3 — Vertical Slice

Desenvolva uma pequena experiência completa que represente a identidade final do jogo.

O vertical slice deverá conter:

- Uma civilização jogável.
- Algumas civilizações inimigas.
- Uma cidade militar inicial.
- Construções funcionais.
- Um conjunto inicial de unidades.
- Divisões.
- Soldados individuais.
- Recrutamento.
- Treinamento.
- Economia.
- Formação militar.
- Uma batalha automática funcional.
- Controle de velocidade.
- Habilidades do general.
- Uma batalha especial contra um chefe.
- Recompensas e progressão.

O vertical slice deve demonstrar a qualidade visual pretendida.

Não aceite um protótipo visualmente rudimentar como representação definitiva da direção artística.

### Fase 4 — Expansão modular

Após validar o vertical slice, desenvolva os módulos restantes.

Ordem sugerida:

1. Sistema completo de soldados e divisões.
2. Sistema de unidades e evoluções.
3. Economia e construções.
4. Tecnologias e eras.
5. Campanha e mapa territorial.
6. Eventos de invasão e defesa.
7. Chefes mitológicos.
8. Prestígio.
9. Recompensas e conquistas.
10. Balanceamento e conteúdo adicional.

A ordem poderá ser ajustada com justificativa técnica.

### Fase 5 — Polimento

Trabalhe continuamente em:

- Arte.
- Animações.
- Interface.
- Áudio.
- Desempenho.
- Balanceamento.
- Usabilidade.
- Feedback visual.
- Correção de bugs.

## 20. CRITÉRIOS DE QUALIDADE E ACEITAÇÃO

O projeto só poderá ser considerado concluído quando atender aos seguintes requisitos.

### Funcionais

- Campanha infinita funcional.
- Civilizações selecionáveis.
- Gerenciamento de exército.
- Soldados individuais.
- Divisões.
- Recrutamento e treinamento.
- Evolução de unidades.
- Construções.
- Economia.
- Tecnologias.
- Eras.
- Prestígio.
- Formações.
- Batalhas automáticas em tempo real.
- Velocidade ajustável.
- Habilidades do general.
- Chefes mitológicos.
- Recompensas permanentes.
- Eventos de invasão e defesa.
- Salvamento e carregamento confiáveis.

### Visuais

- Identidade artística consistente.
- Pixel art bem executada.
- Soldados visualmente distintos.
- Animações convincentes.
- Batalhas visualmente interessantes.
- Chefes mitológicos impressionantes.
- Cidade com evolução visual perceptível.
- Interface profissional.
- Boa legibilidade nas duas telas.

### Técnicos

- Executável para Nintendo DS.
- Compatibilidade com hardware real.
- Uso controlado de memória.
- Desempenho estável.
- Ausência de crashes conhecidos.
- Salvamento confiável.
- Carregamento adequado.
- Código organizado e modular.

Não declare um requisito como concluído apenas porque existe uma implementação parcial.

Cada módulo precisa ter testes e critérios verificáveis.

## 21. REGRAS DE EXECUÇÃO PARA OS AGENTES

1. Trabalhe com autonomia técnica.
2. Não peça aprovação para cada decisão de implementação.
3. Quando houver alternativas, avalie custo, desempenho, qualidade e manutenção.
4. Documente decisões importantes.
5. Preserve sempre a possibilidade de expansão modular.
6. Não implemente sistemas complexos sem validar os fundamentos.
7. Mantenha o projeto compilável durante o desenvolvimento.
8. Não esconda limitações técnicas.
9. Não substitua requisitos importantes por soluções simplificadas sem justificar.
10. Não use placeholders visuais como resultado final.
11. Não sacrifique a identidade artística para acelerar a entrega.
12. Não invente funcionalidades como se estivessem implementadas.
13. Teste continuamente em emulador e, quando possível, em hardware real.
14. Mantenha documentação atualizada.
15. Priorize uma experiência de jogo coesa, satisfatória e divertida.

## 22. DOCUMENTAÇÃO E ENTREGÁVEIS

Mantenha documentação organizada, incluindo:

- Game Design Document.
- Arquitetura técnica.
- Estrutura de módulos.
- Decisões de implementação.
- Orçamentos de hardware.
- Plano de desenvolvimento.
- Registro de testes.
- Problemas conhecidos.
- Instruções de compilação.
- Instruções de execução.
- Controle de funcionalidades implementadas.

Ao final de cada marco, apresente um relatório objetivo com:

1. O que foi desenvolvido.
2. O que está funcionando.
3. O que foi testado.
4. Resultados de desempenho.
5. Problemas encontrados.
6. Próximos passos.

## 23. PRINCÍPIO FINAL DO PRODUTO

O jogo precisa fazer o jogador sentir que está construindo algo grandioso.

Ele começa com poucos soldados, um pequeno acampamento e recursos limitados.

Depois, passa a comandar divisões, desenvolver veteranos, conquistar tecnologias, construir fortalezas, enfrentar civilizações poderosas, derrotar criaturas mitológicas e desafiar deuses.

Seu exército cresce. Seus soldados evoluem. Sua cidade se transforma. Sua reputação aumenta. Suas conquistas permanecem.

Mesmo depois de muitas horas, o jogador precisa encontrar novos objetivos e motivos para continuar.

A experiência deve ser viciante pela qualidade da progressão, pela profundidade dos sistemas, pela satisfação das recompensas e pelo espetáculo das batalhas.

**Desenvolva um jogo de estratégia militar para Nintendo DS que seja bonito, profundo, tecnicamente sólido e genuinamente divertido de jogar.**

Não entregue apenas uma demonstração de sistemas. Construa um jogo.