<h1 align="center">🎛️ ESP32 BLE MIDI Footswitch</h1>
<p align="center">
  Pedal controlador MIDI com interface web integrada, conectividade BLE e 10 footswitches configuráveis.
</p>

<p align="center">
  <img src="https://img.shields.io/badge/MIDI-BLE-blue?style=flat-square">
  <img src="https://img.shields.io/badge/LEDs-RGB-ff69b4?style=flat-square">
  <img src="https://img.shields.io/badge/ESP32-compatible-brightgreen?style=flat-square">
  <img src="https://img.shields.io/badge/Dashboard-Web-orange?style=flat-square">
</p>

---

## ✨ Funcionalidades

- 🎚️ **10 Footswitches Configuráveis**  
  Controle independente com modos CC, PC ou Desligado.

- 📶 **Conectividade Bluetooth MIDI (BLE)**  
  Comunicação sem fio com pedaleiras e dispositivos MIDI BLE.

- 🌐 **Dashboard Web Integrado**
  - Ponto de acesso Wi-Fi criado pelo ESP32.
  - Interface acessível via navegador (PC, celular, tablet).
  - Configuração de botões, LEDs e brilho.

- 🎨 **Feedback Visual com LEDs RGB**
  - Um LED por botão (WS2812B/Neopixel).
  - Cores e brilho configuráveis via dashboard.
  - Lógica de comportamento inteligente (modo toggle ou banco ativo).

- 🧠 **Snapshots de Bancos (modo PC)**
  - Acione combinações de LEDs nos botões CC automaticamente.

- 💾 **Configuração Persistente**
  - Salvo em `config.json` via LittleFS.
  - Retém dados após desligar o pedal.

---

## 🔧 Hardware Necessário

| Componente            | Descrição                                         |
|-----------------------|--------------------------------------------------|
| ESP32 DevKit          | Placa de desenvolvimento com suporte Wi-Fi/BLE   |
| Footswitch x10        | Botões momentâneos (NA)                          |
| WS2812B x10           | LEDs RGB endereçáveis de 5V                      |

---

## 📦 Bibliotecas Utilizadas

> ⚠️ **Use as versões modificadas incluídas neste repositório**.  
> Alterações foram feitas diretamente nos arquivos das bibliotecas. Outras versões **não funcionarão corretamente**.

- `ESPAsyncWebServer` – por Hristo Gochkov
- `AsyncTCP` – por Hristo Gochkov
- `Adafruit NeoPixel` – por Adafruit
- `ArduinoJson` – por Benoit Blanchon (v6.x ou v7.x)
- `BLEMidi` – versão modificada de Max Kellerman
- `DNSServer` – incluída no core ESP32
- `LittleFS` – incluída no core ESP32

---

## ⚙️ Instalação

<details>
  <summary><strong>1. Configurar a IDE Arduino</strong></summary>

- Instale o suporte ESP32 no Gerenciador de Placas.
- Selecione a placa: `ESP32 Dev Module`.
- Em **Esquema de Partição**, selecione:
  - `Huge APP (3MB No OTA/1MB SPIFFS)` ou
  - `Minimal SPIFFS (Large APPS)`
- Em **Core Debug Level**: selecione `None` ou `Error`.

</details>

<details>
  <summary><strong>2. Instalar Bibliotecas</strong></summary>

- Copie as bibliotecas modificadas incluídas no repositório para:

```bash
Documentos/Arduino/libraries/
```

</details>

<details>
  <summary><strong>3. Dashboard Web (LittleFS)</strong></summary>

- Crie uma pasta `data` na mesma pasta do seu `.ino`
- Coloque os arquivos `index.html` e `style.css` nela.
- Faça upload via **Ferramentas > ESP32 Sketch Data Upload**  
  (Feche o Serial Monitor antes!)

</details>

<details>
  <summary><strong>4. Upload do Código</strong></summary>

- Compile e carregue o `.ino` no seu ESP32 normalmente.

</details>

---

## 🚀 Como Usar

1. **Ligue o pedal** – ESP32 entra em modo de configuração via Wi-Fi.
2. **Conecte-se à rede Wi-Fi**:

```bash
SSID: ESP32_MIDI_Config
Senha: midi12345
```

3. **Acesse o dashboard** via navegador:

```
http://192.168.4.1
```

4. **Configure os botões**:
   - Modo: `Desligado`, `CC`, `PC`
   - Canal MIDI: `1–16`
   - Número CC/PC: `0–127`
   - Valor CC (se aplicável)
   - Cor do LED

5. **Modo PC** permite configurar LEDs que serão ligados nos botões CC ao ativar um banco.

6. **Clique em “Salvar Todas as Configurações”** para gravar no ESP32.

7. **Conecte à sua pedaleira MIDI BLE**:  
   O ESP32 se conectará automaticamente ao dispositivo alvo (`BlackBox_BLE`).


## 🧪 Estado Atual

- ✅ 100% funcional com 10 footswitches
- ✅ Dashboard responsivo
- ✅ BLE MIDI funcionando
- ✅ Configuração persistente com LittleFS

---

## 📄 Licença

Distribuído sob a licença MIT. Veja [`LICENSE`](./LICENSE) para mais detalhes.
