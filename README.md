🌊 Alerta Cidadão — Monitoramento e Prevenção de Enchentes
FIAP · Global Solution 2026.2 · Edge Computing & Computer Systems
Projeto acadêmico CitizenTech desenvolvido para demonstrar o monitoramento do nível da água e a identificação de situações de risco por meio de um sistema embarcado.
O protótipo utiliza Arduino Uno e sensores para realizar medições, processar informações localmente e emitir alertas visuais e sonoros.

🎯 1. Objetivo do projeto
O Alerta Cidadão tem como objetivo demonstrar uma solução tecnológica de monitoramento preventivo de enchentes em áreas urbanas.
O dispositivo simula o monitoramento de um córrego ou canal, identificando alterações no nível da água e informando diferentes condições de risco.
O sistema realiza todo o processamento localmente, sem depender de conexão com a internet ou de servidores externos.
Importante: este projeto é um protótipo acadêmico simulado. Não substitui sistemas oficiais de monitoramento, previsão ou segurança contra enchentes.

💡 2. Problema e proposta
Em áreas urbanas sujeitas a alagamentos, a elevação rápida do nível da água pode representar riscos para moradores, pedestres e motoristas.
A proposta é desenvolver um dispositivo capaz de:
Monitorar a distância até a superfície da água.
Estimar o nível da água.
Identificar condições NORMAL, ATENÇÃO e ALERTA.
Emitir avisos visuais e sonoros.
Exibir informações em um display LCD.
Continuar funcionando sem conexão externa.

🔧 3. Componentes utilizados
ComponenteFunçãoArduino UnoUnidade principal de processamentoHC-SR04Medição da distância até a superfície da águaDHT22Medição de temperatura e umidade do ambienteLCD 16x2 I2CExibição de informações e alertasLED verdeIndicação de estado NORMALLED amareloIndicação de estado ATENÇÃOLED vermelhoIndicação de estado ALERTABuzzerEmissão de avisos sonorosProtoboardOrganização das conexõesResistoresLimitação de corrente dos LEDs

⚙️ 4. Funcionamento do sistema
O funcionamento segue o ciclo:
MEDIR → PROCESSAR → DECIDIR → ALERTAR
O HC-SR04 mede a distância entre o sensor e a superfície da água.
O Arduino valida as leituras e aplica suavização por média móvel.
O sistema calcula o nível estimado da água.
Os valores são comparados com limites configurados.
O estado correspondente é identificado.
Os LEDs, o buzzer e o LCD apresentam a resposta.
Estados de operação
EstadoIndicaçãoNORMALLED verdeATENÇÃOLED amarelo e aviso sonoroALERTALED vermelho, buzzer e animação de perigoFALHASinalização de falha nas leituras do sensor principal

📏 5. Cálculo do nível da água
O sensor HC-SR04 mede a distância até a superfície da água.
O sistema utiliza uma altura de referência configurável para estimar o nível:
Nível estimado = Altura de referência - Distância medida
Exemplo:
Altura de referência: 200 cm
Distância medida: 150 cm
Nível estimado: 50 cm
Quanto menor a distância medida, maior é o nível estimado da água.
Os limites utilizados são parâmetros de demonstração e podem ser configurados conforme a lógica do protótipo.

🧠 6. Recursos de processamento
Média móvel: reduz oscilações nas medições.
Validação de leituras: identifica medições inválidas.
Histerese: evita mudanças constantes de estado próximas aos limites.
EEPROM: armazena configurações para serem recuperadas após reinicializações.
millis(): controla intervalos de execução sem depender de longos atrasos.
Monitor Serial: apresenta informações e permite comandos de configuração.

🖥️ 7. Interface LCD
O display LCD 16x2 apresenta informações do sistema.
O projeto também utiliza caracteres personalizados para criar recursos visuais:
Animação de ondas durante a inicialização.
Identificação visual do estado de ALERTA.
Símbolo de perigo acompanhado da mensagem PERIGO!.
Exibição do nível estimado da água.

📟 8. Monitor Serial
O Monitor Serial permite acompanhar as medições e o estado do sistema.
Exemplo de saída:
[33.3s] estado=NORMAL | bruta=150.5cm |
media=150.5cm | nivel=49.5cm |
invalidas=0 | T=25.0C U=60%
Informações apresentadas:
Tempo de funcionamento.
Estado atual.
Distância bruta medida.
Média das medições.
Nível estimado da água.
Quantidade de leituras inválidas.
Temperatura e umidade do ambiente.

🛠️ 9. Tecnologias utilizadas
Arduino Uno
Linguagem C/C++
Arduino IDE / Wokwi
Sensor ultrassônico HC-SR04
Sensor DHT22
Display LCD I2C
EEPROM
Git e GitHub

▶️ 10. Como executar o projeto
Acesse o link público da simulação Wokwi.
Abra o projeto no simulador.
Inicie a simulação pelo botão de execução.
Observe a animação de inicialização no LCD.
Abra o Monitor Serial para acompanhar as medições.
Altere os valores simulados do sensor para observar os diferentes estados.
Verifique as respostas dos LEDs, buzzer e display.
Arquivos do projeto
ALERTA-CIDADAO/
│
├── diagram.json
├── sketch.ino
├── libraries.txt
└── README.md

🌎 11. Objetivos de Desenvolvimento Sustentável (ODS)
ODS 11 — Cidades e Comunidades Sustentáveis: soluções tecnológicas para cidades mais seguras e resilientes.
ODS 13 — Ação Contra a Mudança Global do Clima: demonstração de recursos de monitoramento e prevenção de riscos climáticos.
ODS 9 — Indústria, Inovação e Infraestrutura: aplicação de tecnologia e inovação em uma solução de monitoramento.

👥 12. Equipe
João Pedro Castanheira Perez — RM 576203
Murillo Kirsten Minamoto — RM 576741
Heitor Zanelato — RM 576542
Breno Uccelli — RM 576444

🔗 13. Links da entrega
Projeto Wokwi: https://wokwi.com/projects/479852470329788417
Vídeo de apresentação: PREENCHER COM O LINK DO YOUTUBE
FIAP — Global Solution 2026.2
Projeto acadêmico desenvolvido na temática CitizenTech, com foco em Edge Computing e monitoramento inteligente de enchentes.