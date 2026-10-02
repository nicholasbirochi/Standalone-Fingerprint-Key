# Standalone Fingerprint Key

Firmware MVP para ESP32-S3 e sensor biométrico HLK-ZW111. Uma digital reconhecida envia **F13** por USB HID; o dispositivo não armazena nem digita senhas.

## Hardware

| HLK-ZW111 | ESP32-S3 |
| --- | --- |
| VCC | 3V3 |
| GND | GND |
| TX | GPIO4 (RX do ESP32) |
| RX | GPIO5 (TX do ESP32) |
| VT | 3V3 |

TX do sensor vai no RX do ESP32; RX do sensor vai no TX do ESP32. VT fica permanentemente ligado a 3V3. Não conecte TOUCH. GPIO6 e GPIO7 não são usados. Não use 5V sem confirmação específica para o seu módulo.

Mais detalhes em [docs/wiring.md](docs/wiring.md) e [docs/troubleshooting.md](docs/troubleshooting.md).

## Arduino IDE

- Instale o pacote de placas **esp32 by Espressif Systems**.
- Abra `firmware/StandaloneFingerprintKey/StandaloneFingerprintKey.ino`.
- Board: **ESP32S3 Dev Module**.
- USB CDC On Boot: **Enabled**.
- USB Mode: **USB-OTG (TinyUSB)** ou a opção equivalente que habilite USB nativo e HID. Os nomes variam conforme a versão do Arduino ESP32 Core.
- Selecione a porta da conexão USB nativa do ESP32-S3 e carregue o sketch.
- Serial Monitor: **115200 baud**.

Use PlatformIO alternativamente, a partir de `firmware/`:

```sh
pio run -e esp32s3
pio run -e esp32s3 -t upload
pio device monitor -b 115200
```

## Protocolo do sensor

O firmware conserva o protocolo de pacotes `EF 01` e os comandos HLK-ZW111 já implementados no projeto: verificar a senha padrão do módulo, capturar imagem, gerar características, pesquisar, combinar e salvar modelo, apagar modelo e consultar a contagem. UART configurada como **57600 baud, 8N1**, igual à configuração anterior do projeto. O baud é uma configuração serial, não parte do frame.

O código e a documentação anterior identificam o framing/checksum como compatíveis com a família ZhianTec/Synochip usada pelo ZW111. Isso não substitui teste no módulo físico: o protocolo nunca foi validado neste projeto com um HLK-ZW111 conectado. Se o módulo tiver baud ou senha de comunicação alterados, `status` pode falhar.

O comando de contagem do protocolo retorna apenas a quantidade de modelos, não uma lista de IDs. Por isso `list` exibe a contagem e informa a limitação. `enroll` exige um ID explícito para não sobrescrever uma impressão quando houver lacunas nos IDs.

## Primeiro teste

1. Carregue o firmware.
2. Abra o Serial Monitor em 115200 baud.
3. Execute `status` e confirme `[SENSOR] conectado`.
4. Execute `test`.
5. Abra um aplicativo no Mac ou um visualizador de eventos de teclado.
6. Confirme que o evento F13 foi recebido.
7. Execute `enroll 0` (ou outro ID livre entre 0 e 49).
8. Siga as instruções para colocar, retirar e colocar novamente o mesmo dedo.
9. Coloque a digital cadastrada e confirme o ID no Serial Monitor e o evento F13 no Mac.

O F13 não digita Enter nem texto. Associe-o depois a uma ação no macOS com Karabiner-Elements, BetterTouchTool ou software equivalente.

## Comandos seriais

| Comando | Ação |
| --- | --- |
| `help` | Mostra os comandos disponíveis. |
| `status` | Verifica se o sensor responde. |
| `enroll <id>` | Cadastra uma digital no ID informado, de 0 a 49. |
| `delete <id>` | Apaga o modelo no ID informado. |
| `list` | Mostra a contagem; o protocolo não fornece os IDs cadastrados. |
| `test` | Envia F13 sem usar o sensor. |

## Diagnóstico

O boot informa os pinos e a configuração UART. Em repouso, a resposta `sem dedo` do sensor não é impressa repetidamente. Erros de leitura são reportados uma vez e a leitura automática pausa; use `status` depois de conferir a ligação para testar novamente.

Consulte [docs/troubleshooting.md](docs/troubleshooting.md) para sensor sem resposta, USB HID e seleção de porta.
