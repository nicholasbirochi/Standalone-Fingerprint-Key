# Standalone Fingerprint Key

Firmware para ESP32-S3 e HLK-ZW111: uma digital reconhecida envia F13 por USB HID ao macOS. Não armazena nem digita senhas.

## Hardware

| HLK-ZW111 | ESP32-S3 |
| --- | --- |
| VCC | 3V3 |
| GND | GND |
| TX | GPIO4 (RX) |
| RX | GPIO5 (TX) |
| VT | 3V3 |
| TOUCH | não conectado |

TX do sensor -> RX do ESP32. RX do sensor -> TX do ESP32. Não use 5V sem confirmação específica para o módulo.

## Funcionamento

`HLK-ZW111 -> UART -> ESP32-S3 -> USB HID F13 -> macOS`

O sensor usa UART 57600 baud, 8N1. O firmware mantém o framing e os comandos HLK-ZW111 já existentes.

## Firmware

O sketch está em `firmware/StandaloneFingerprintKey.ino`.

Arduino IDE: instale **esp32 by Espressif Systems**, selecione **ESP32S3 Dev Module**, habilite **USB CDC On Boot**, escolha **USB-OTG (TinyUSB)** ou modo USB nativo equivalente, e abra o Serial Monitor em 115200 baud. Os nomes das opções variam entre versões do Arduino ESP32 Core. Use a porta da conexão USB nativa do ESP32-S3.

PlatformIO, a partir de `firmware/`:

```sh
pio run -e esp32s3
pio run -e esp32s3 -t upload
pio device monitor -b 115200
```

## Comandos

| Comando | Ação |
| --- | --- |
| `help` | Mostra os comandos. |
| `status` | Testa a comunicação com o sensor. |
| `enroll <id>` | Cadastra uma digital no ID 0..49. |
| `delete <id>` | Remove o ID informado. |
| `list` | Mostra a quantidade; o sensor não fornece uma lista de IDs. |
| `test` | Envia F13 sem usar o sensor. |

## Primeiro teste

1. Grave o firmware.
2. Abra o Serial Monitor em 115200.
3. Execute `status` e confirme que o sensor respondeu.
4. Execute `test`.
5. Confirme F13 no Mac com um visualizador de eventos de teclado.
6. Execute `enroll 0` e siga as instruções para cadastrar o mesmo dedo duas vezes.
7. Teste a digital e confirme F13 no Mac.

## Problemas comuns

- **Sensor não responde:** confira VCC 3V3, GND e VT em 3V3; execute `status` novamente.
- **TX/RX invertidos:** sensor TX vai a GPIO4; sensor RX vai a GPIO5.
- **VT não conectado:** ligue VT a 3V3; o sensor fica sempre alimentado.
- **USB HID não aparece:** use cabo de dados e conexão USB nativa do ESP32-S3.
- **Porta USB errada:** algumas placas têm uma porta USB-UART separada; use a porta nativa para HID.
- **Configuração USB incorreta:** habilite USB CDC On Boot e selecione USB-OTG/TinyUSB ou equivalente.
