# Ligação

| HLK-ZW111 | ESP32-S3 | Observação |
| --- | --- | --- |
| VCC | 3V3 | Não use 5V sem confirmação específica para o módulo. |
| GND | GND | Terra comum. |
| TX | GPIO4 (RX) | TX do sensor vai no RX do ESP32. |
| RX | GPIO5 (TX) | RX do sensor vai no TX do ESP32. |
| VT | 3V3 | Alimentação permanentemente ativa. |

TOUCH não é conectado. GPIO6 e GPIO7 não são utilizados. Confira a identificação dos pinos na placa e no chicote do seu sensor antes de ligar a alimentação.

A UART do firmware usa **57600 baud, 8N1**. A implementação preserva o enquadramento `EF 01`, soma de verificação e comandos HLK-ZW111 que já existiam no projeto. A comunicação ainda precisa ser confirmada com o módulo físico; consulte o [README](../README.md#protocolo-do-sensor).
