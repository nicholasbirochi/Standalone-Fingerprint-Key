# Troubleshooting

## Sensor não responde

- Confira VCC em 3V3, GND comum e VT ligado a 3V3.
- Confira UART cruzada: TX do sensor -> GPIO4; RX do sensor -> GPIO5.
- Confirme no Serial Monitor a linha `[UART] RX GPIO4 / TX GPIO5 / 57600 baud / 8N1`.
- Execute `status` depois de revisar os fios. O firmware usa a senha padrão `00000000` para verificar o módulo; um módulo reconfigurado pode não responder.
- Não ligue em 5V sem confirmação específica para o módulo.

## TX/RX invertidos

TX e RX devem ser cruzados. TX do sensor é entrada RX GPIO4 do ESP32; RX do sensor é saída TX GPIO5 do ESP32.

## VT não conectado

Ligue VT a 3V3. Neste MVP o sensor fica sempre alimentado; não há controle de energia pelo firmware.

## USB HID não aparece

- Use um cabo USB com dados e a conexão USB nativa do ESP32-S3, não a porta do conversor USB-UART.
- Selecione **ESP32S3 Dev Module**, habilite **USB CDC On Boot** e escolha **USB-OTG (TinyUSB)** ou opção equivalente de USB nativo/HID.
- As opções variam conforme a versão do Arduino ESP32 Core e o modelo da placa.
- Para upload e Serial Monitor, selecione a porta correspondente à conexão USB nativa.

## Upload funciona, mas HID não

`test` envia F13 sem envolver o sensor. Se o Serial Monitor funciona, mas nenhum evento aparece, confira a porta USB física, o modo USB-OTG/TinyUSB e se o macOS enumerou o ESP32-S3 como teclado. A conexão USB nativa precisa estar ligada ao computador; algumas placas têm uma segunda porta apenas para UART.

## Sensor reconhece, mas nenhuma tecla chega ao Mac

- Execute `test` para isolar USB HID da leitura biométrica.
- Confirme no Serial Monitor `[MATCH] ID=... confidence=...` e depois `[HID] F13 enviado`.
- Se `test` também falhar, volte à configuração USB nativa/TinyUSB e à conexão física; o sensor não é a causa.
- Use um visualizador de eventos de teclado: F13 pode não produzir caracteres visíveis em um campo de texto.
