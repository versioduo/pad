// © Kay Sievers <kay@versioduo.com>, 2020-2022
// SPDX-License-Identifier: Apache-2.0

#include <V2Base.h>
#include <V2Color.h>
#include <V2Device.h>
#include <V2FSR.h>
#include <V2LED.h>
#include <V2Link.h>
#include <V2MIDI.h>

V2DEVICE_METADATA("com.versioduo.pad", 54, "versioduo:samd:pad");

static V2LED::WS2812 LED(24, PIN_LED_WS2812, &sercom2, SPI_PAD_0_SCK_1, PIO_SERCOM);
static V2Base::Analog::ADC ADC(V2Base::Analog::ADC::getID(PIN_FSR_SENSE));
static V2Link::Port Plug(&SerialPlug);
static V2Link::Port Socket(&SerialSocket);

static class Device : public V2Device {
public:
  Device() : V2Device() {
    metadata.vendor      = "Versio Duo";
    metadata.product     = "V2 pad";
    metadata.description = "Drum Pad";
    metadata.home        = "https://versioduo.com/#pad";

    system.download  = "https://versioduo.com/download";
    system.configure = "https://versioduo.com/configure";

    configuration = {.size{sizeof(config)}, .data{&config}};
  }

  enum class CC {
    Color      = V2MIDI::CC::Controller14,
    Saturation = V2MIDI::CC::Controller15,
    Brightness = V2MIDI::CC::Controller89,
    Rainbow    = V2MIDI::CC::Controller90,
  };

  // Config, written to EEPROM
  struct {
    uint8_t channel{};
    uint8_t controller{};
    uint8_t note{V2MIDI::GM::Percussion::AcousticSnare};
    bool aftertouch{true};
    float sensitivity{};
    struct {
      uint8_t h{};
      uint8_t s{};
      uint8_t v{100};
    } color{.v{100}};
  } config;

  void light(float fraction) {
    // Set minimal brightness.
    if (fraction > 0 && fraction < 0.1f)
      fraction += 0.01f;

    LED.setHSV(_led.h, _led.s, _led.v * fraction);
  }

  void play(uint8_t channel, uint8_t note, uint8_t velocity) {
    // Echo / loopback / test, send the note back to the sender.
    if (note == V2MIDI::B(2)) {
      V2MIDI::Packet midi{};
      send(midi.setNote(channel, V2MIDI::B(2), velocity));
      LED.splashHSV(0.01, 0, 0, (float)velocity / 127.f);
      return;
    }

    if (note < V2MIDI::C(3))
      return;

    const uint8_t color = note - V2MIDI::C(3);
    if (color >= V2Base::countof(_leds) + 1)
      return;

    if (velocity > 0) {
      const float fraction = (float)velocity / 127.f;
      if (color == 0)
        LED.setHSV(_led.h, _led.s, fraction * _led.v);

      else
        LED.setHSV(_leds[color - 1].hue, 1, fraction * _led.v);
      _note = note;

    } else {
      _note = 0;
      LED.setBrightness(0);
    }
  }

private:
  struct {
    float h;
    float s;
    float v;
  } _led{};

  const struct {
    const char *name;
    float hue;
  } _leds[6]{
    {.name{"Red"}, .hue{V2Color::Red}},
    {.name{"Yellow"}, .hue{V2Color::Yellow}},
    {.name{"Green"}, .hue{V2Color::Green}},
    {.name{"Cyan"}, .hue{V2Color::Cyan}},
    {.name{"Blue"}, .hue{V2Color::Blue}},
    {.name{"Magenta"}, .hue{V2Color::Magenta}},
  };

  float _rainbow{};
  uint8_t _note{};

  void handleReset() override {
    _led.h = (float)config.color.h / 127.f * 360.f;
    _led.s = (float)config.color.s / 127.f;
    _led.v = (float)config.color.v / 127.f;
    allNotesOff();
  }

  void allNotesOff() {
    _rainbow = 0;
    LED.reset();
  }

  bool handleSend(V2MIDI::Packet *midi) override {
    usb.midi.send(midi);
    Plug.send(midi);
    return true;
  }

  void handleNote(uint8_t channel, uint8_t note, uint8_t velocity) override {
    if (channel != config.channel)
      return;

    play(channel, note, velocity);
  }

  void handleNoteOff(uint8_t channel, uint8_t note, uint8_t velocity) override {
    if (channel != config.channel)
      return;

    play(channel, note, 0);
  }

  void handleControlChange(uint8_t channel, uint8_t controller, uint8_t value) override {
    if (channel != config.channel)
      return;

    switch (controller) {
      case (uint8_t)CC::Color:
        _led.h = (float)value / 127.f * 360.f;
        break;

      case (uint8_t)CC::Saturation:
        _led.s = (float)value / 127.f;
        break;

      case (uint8_t)CC::Brightness:
        _led.v = (float)value / 127.f;
        if (_rainbow > 0.f)
          LED.rainbow(1, 4.5f - (_rainbow * 4.f), _led.v, true);
        break;

      case (uint8_t)CC::Rainbow:
        _rainbow = (float)value / 127.f;
        if (_rainbow <= 0.f)
          LED.reset();
        else
          LED.rainbow(1, 4.5f - (_rainbow * 4.f), _led.v, true);
        break;

      case V2MIDI::CC::AllSoundOff:
      case V2MIDI::CC::AllNotesOff:
        allNotesOff();
        break;
    }
  }

  void handleAftertouch(uint8_t channel, uint8_t note, uint8_t pressure) override {
    if (channel != config.channel)
      return;

    if (note != _note)
      return;

    const uint8_t color  = note - V2MIDI::C(3);
    const float fraction = (float)pressure / 127.f;
    if (color == 0)
      LED.setHSV(_led.h, _led.s, _led.v * fraction);

    else
      LED.setHSV(_leds[color - 1].hue, 1, _led.v * fraction);
  }

  void handleSystemReset() override {
    reset();
  }

  void exportSettings(JsonArray json) override {
    {
      JsonObject setting = json.add<JsonObject>();
      setting["type"]    = "number";
      setting["title"]   = "MIDI";
      setting["label"]   = "Channel";
      setting["min"]     = 1;
      setting["max"]     = 16;
      setting["input"]   = "select";
      setting["path"]    = "midi/channel";
    }

    {
      JsonObject setting = json.add<JsonObject>();
      setting["type"]    = "drum";
      setting["title"]   = "Pad";
      setting["path"]    = "drum";
    }

    {
      JsonObject setting = json.add<JsonObject>();
      setting["type"]    = "color";
      setting["title"]   = "Light";
      setting["path"]    = "color";
    }
  }

  void exportConfiguration(JsonObject json) override {
    {
      json["#midi"]         = "The MIDI settings";
      JsonObject jsonMidi  = json["midi"].to<JsonObject>();
      jsonMidi["#channel"] = "The channel to send notes and control values to";
      jsonMidi["channel"]  = config.channel + 1;
    }

    {
      json["#drum"]           = "The drum's MIDI settings";
      JsonObject jsonDrum     = json["drum"].to<JsonObject>();
      jsonDrum["#controller"] = "The controller number, 0 = disabled";
      jsonDrum["controller"]  = config.controller;

      jsonDrum["#note"] = "The note number";
      jsonDrum["note"]  = config.note;

      jsonDrum["#aftertouch"] = "Send polyphonic aftertouch";
      jsonDrum["aftertouch"]  = config.aftertouch;

      jsonDrum["#sensitivity"] = "The sensitivity of the sensor (-0.99 .. 0.99)";
      jsonDrum["sensitivity"]  = serialized(String(config.sensitivity, 2));
    }

    {
      json["#color"]    = "The pad color. Hue, saturation, brightness, 0..127";
      JsonArray jsonLed = json["color"].to<JsonArray>();
      jsonLed.add(config.color.h);
      jsonLed.add(config.color.s);
      jsonLed.add(config.color.v);
    }
  }

  void importConfiguration(JsonObject json) override {
    JsonObject jsonMidi = json["midi"];
    if (jsonMidi) {
      if (!jsonMidi["channel"].isNull()) {
        uint8_t channel = jsonMidi["channel"];

        if (channel < 1)
          config.channel = 0;
        else if (channel > 16)
          config.channel = 15;
        else
          config.channel = channel - 1;
      }
    }

    JsonObject jsonDrum = json["drum"];
    if (jsonDrum) {
      if (!jsonDrum["controller"].isNull()) {
        config.controller = jsonDrum["controller"];

        if (config.controller > 127)
          config.controller = 127;
      }

      if (!jsonDrum["note"].isNull()) {
        config.note = jsonDrum["note"];

        if (config.note > 127)
          config.note = 127;
      }

      if (!jsonDrum["aftertouch"].isNull())
        config.aftertouch = jsonDrum["aftertouch"];

      if (!jsonDrum["sensitivity"].isNull()) {
        float sensitivity = jsonDrum["sensitivity"];

        if (sensitivity <= -1.f || sensitivity >= 1.f)
          sensitivity = 0;

        config.sensitivity = sensitivity;
      }
    }

    JsonArray jsonLed = json["color"];
    if (jsonLed) {
      uint8_t color = jsonLed[0];
      if (color > 127)
        color = 127;
      config.color.h = color;

      uint8_t saturation = jsonLed[1];
      if (saturation > 127)
        saturation = 127;
      config.color.s = saturation;

      uint8_t brightness = jsonLed[2];
      if (brightness > 127)
        brightness = 127;
      config.color.v = brightness;
    }

    reset();
  }

  void exportInput(JsonObject json) override {
    json["channel"] = config.channel;

    // List of notes to play
    JsonArray jsonNotes = json["notes"].to<JsonArray>();
    {
      JsonObject jsonEcho = jsonNotes.add<JsonObject>();
      jsonEcho["name"]    = "Echo Test";
      jsonEcho["number"]  = V2MIDI::B(2);
    }

    {
      JsonObject jsonLed = jsonNotes.add<JsonObject>();
      jsonLed["name"]    = "Default";
      jsonLed["number"]  = V2MIDI::C(3);
    }

    for (uint8_t i = 0; i < V2Base::countof(_leds); i++) {
      JsonObject jsonLed = jsonNotes.add<JsonObject>();
      jsonLed["name"]    = _leds[i].name;
      jsonLed["number"]  = V2MIDI::C(3) + 1 + i;
    }

    JsonArray jsonControllers = json["controllers"].to<JsonArray>();
    {
      JsonObject jsonController = jsonControllers.add<JsonObject>();
      jsonController["name"]    = "Hue";
      jsonController["number"]  = (uint8_t)CC::Color;
      jsonController["value"]   = (uint8_t)(_led.h / 360.f * 127.f);
    }
    {
      JsonObject jsonController = jsonControllers.add<JsonObject>();
      jsonController["name"]    = "Saturation";
      jsonController["number"]  = (uint8_t)CC::Saturation;
      jsonController["value"]   = (uint8_t)(_led.s * 127.f);
    }
    {
      JsonObject jsonController = jsonControllers.add<JsonObject>();
      jsonController["name"]    = "Brightness";
      jsonController["number"]  = (uint8_t)CC::Brightness;
      jsonController["value"]   = (uint8_t)(_led.v * 127.f);
    }
    {
      JsonObject jsonController = jsonControllers.add<JsonObject>();
      jsonController["name"]    = "Rainbow";
      jsonController["number"]  = (uint8_t)CC::Rainbow;
      jsonController["value"]   = (uint8_t)(_rainbow * 127.f);
    }
  }

  void exportOutput(JsonObject json) override {
    json["channel"] = config.channel;

    if (config.controller > 0) {
      JsonArray jsonController = json["controllers"].to<JsonArray>();
      JsonObject jsonPressure = jsonController.add<JsonObject>();
      jsonPressure["name"]    = "Pressure";
      jsonPressure["number"]  = config.controller;
    }

    JsonArray jsonNotes = json["notes"].to<JsonArray>();
    JsonObject note      = jsonNotes.add<JsonObject>();
    note["name"]         = "Drum";
    note["number"]       = config.note;
    note["aftertouch"]   = true;
  }
} Device;

static class FSR : public V2FSR {
public:
  FSR() : V2FSR(&_config){};

private:
  const V2FSR::Config _config{
    .nSteps{128},
    .alpha{0.5},
    .lag{0.01},
    .pressure{.min{0.2}, .max{0.9}, .exponent{2}},
    .hit{.min{0.05},
         .max{0.7},
         .exponent{1},
         .risingUsec{5 * 1000},
         .holdUsec{5 * 1000},
         .pressureDelayUsec{50 * 1000},
         .releaseUsec{50 * 1000}},
    .release{.minUsec{10 * 1000}, .maxUsec{300 * 1000}},
  };

  V2MIDI::Packet _midi{};
  bool _noteOn{};

  float handleMeasurement() override {
    // Apply sensitivity calibration. Convert the configuration value
    // -0.99 .. 0.99 to 0.5 .. 2.
    const float exponent = powf(3, -Device.config.sensitivity);
    return powf(ADC.read(), exponent);
  }

  void handlePressureRaw(float fraction, uint16_t step) override {
    Device.light(fraction);
  }

  void handlePressure(float fraction, uint16_t step) override {
    if (_noteOn && Device.config.aftertouch) {
      _midi.setAftertouch(Device.config.channel, Device.config.note, step);
      Device.send(&_midi);
    }

    if (Device.config.controller > 0) {
      _midi.setControlChange(Device.config.channel, Device.config.controller, step);
      Device.send(&_midi);
    }
  }

  void handleHit(uint8_t velocity) override {
    Device.led.flash(0.03, 0.3);
    _noteOn = true;
    _midi.setNote(Device.config.channel, Device.config.note, velocity);
    Device.send(&_midi);
  }

  void handleRelease(uint8_t velocity) override {
    _noteOn = false;
    _midi.setNoteOff(Device.config.channel, Device.config.note, velocity);
    Device.send(&_midi);
  }
} FSR;

// Dispatch MIDI packets
static class MIDI {
public:
  void loop() {
    if (!Device.usb.midi.receive(&_midi))
      return;

    if (_midi.getPort() == 0) {
      Device.dispatch(&Device.usb.midi, &_midi);

    } else {
      _midi.setPort(_midi.getPort() - 1);
      Socket.send(&_midi);
    }
  }

private:
  V2MIDI::Packet _midi{};
} MIDI;

// Dispatch Link packets
static class Link : public V2Link {
public:
  Link() : V2Link(&Plug, &Socket) {}

private:
  V2MIDI::Packet _midi{};

  // Receive a host event from our parent device
  void receivePlug(V2Link::Packet *packet) override {
    if (packet->getType() == V2Link::Packet::Type::MIDI) {
      packet->receive(&_midi);
      Device.dispatch(&Plug, &_midi);
    }
  }

  // Forward children device events to the host
  void receiveSocket(V2Link::Packet *packet) override {
    if (packet->getType() == V2Link::Packet::Type::MIDI) {
      uint8_t address = packet->getAddress();
      if (address == 0x0f)
        return;

      if (Device.usb.midi.connected()) {
        packet->receive(&_midi);
        _midi.setPort(address + 1);
        Device.usb.midi.send(&_midi);
      }
    }
  }
} Link;

void setup() {
  Serial.begin(9600);

  LED.begin();
  LED.setMaxBrightness(0.5);

  Plug.begin();
  Socket.begin();
  Device.link = &Link;

  // Set the SERCOM interrupt priority, it requires a stable ~300 kHz interrupt
  // frequency. This needs to be after begin().
  setSerialPriority(&SerialPlug, 2);
  setSerialPriority(&SerialSocket, 2);

  ADC.begin();
  ADC.sampleChannel(V2Base::Analog::ADC::getChannel(PIN_FSR_SENSE));

  Device.begin();
  Device.reset();
}

void loop() {
  LED.loop();
  MIDI.loop();
  Link.loop();
  FSR.loop();
  Device.loop();

  if (Link.idle() && Device.idle())
    Device.sleep();
}
