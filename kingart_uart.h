#include "esphome.h"

#define BUFFER_SIZE     140
static const char *TAG = "KINGARTS";

struct PS16DZ {
  char *tx_buffer = nullptr;                 // Serial transmit buffer
  char *rx_buffer = nullptr;                 // Serial receive buffer
  int byte_counter = 0;
  uint8_t dimmer = 0;
  bool switch_state = false;
} Ps16dz;


class KingArtsComponent : public Component, public UARTDevice, public FloatOutput {
	public:
		KingArtsComponent(UARTComponent *parent, light::LightState *dimmer) : UARTDevice(parent),dimmer_(dimmer){}
  protected:
    bool power = false;
    uint8_t light_dimmer = 0;
    light::LightState *dimmer_{nullptr};


  void write_state(float state) override {
    if (id(set_by_touch))
    {   /* Prevent sending the value back to the hardware */
        id(set_by_touch) = false;
        return;
    }

    // Write to HW
    uint8_t dimmerVal = 0;
    
    if (state > 0.0)
    {
        dimmerVal = state * 100;
    }

    ESP_LOGD(TAG, "Writing dimmer value %d", dimmerVal);
    ESP_LOGD(TAG, "Writing dimmer value %d", dimmerVal);
    SerialSendUpdateCommand(dimmerVal,dimmerVal != 0);
  }
	void SerialSendTxBuffer(void) {
		ESP_LOGD(TAG, "PSZ: Send %s", Ps16dz.tx_buffer);
		print(Ps16dz.tx_buffer);
		write(0x1B);
	  	flush();
	   }

	void SerialSendOkCommand(void){
    ESP_LOGD(TAG, "Send OK");
		snprintf_P(Ps16dz.tx_buffer, BUFFER_SIZE, PSTR("AT+SEND=ok"));
  	SerialSendTxBuffer();
	}


	void SerialInput(void){
		while (available()) {
      yield();
      uint8_t serial_in_byte = read();
      if (serial_in_byte != 0x1B) {
        if (Ps16dz.byte_counter >= BUFFER_SIZE - 1) {
          memset(Ps16dz.rx_buffer, 0, BUFFER_SIZE);
          Ps16dz.byte_counter = 0;
        }
        if (Ps16dz.byte_counter || (!Ps16dz.byte_counter && ('A' == serial_in_byte))) {
          Ps16dz.rx_buffer[Ps16dz.byte_counter++] = serial_in_byte;
        }
      }else {
        Ps16dz.rx_buffer[Ps16dz.byte_counter++] = 0x00;

        // AT+RESULT="sequence":"1554682835320"
        ESP_LOGD(TAG, "PSZ: Received %s", Ps16dz.rx_buffer);

        if (!strncmp(Ps16dz.rx_buffer+3, "UPDATE", 6)) {
        // AT+UPDATE="switch":"on","light_type":1,"colorR":255,"colorG":255,"colorB":255,"bright":100,"mode":19,"speed":100,"sensitive":100
          char *end_str;
          char *string = Ps16dz.rx_buffer+10;
          char *token = strtok_r(string, ",", &end_str);

          bool is_switch_change = false;
          bool is_brightness_change = false;
          while (token != nullptr) {
            char* end_token;
            char* token2 = strtok_r(token, ":", &end_token);
            char* token3 = strtok_r(nullptr, ":", &end_token);

            if (!strncmp(token2, "\"switch\"", 8)) {
              Ps16dz.switch_state = !strncmp(token3, "\"on\"", 4) ? true : false;
              ESP_LOGD(TAG, "PSZ: Switch %d", Ps16dz.switch_state);

              is_switch_change = (Ps16dz.switch_state != power);
              if (is_switch_change) {
                if(!power){
                  id(set_by_touch) = true;
                  auto call = dimmer_->turn_on();
                  call.perform();   
                }else{
                  id(set_by_touch) = true;
                  auto call = dimmer_->turn_off();
                  call.perform();  
                }

                power = !power;
                
              }
            }  else if (!strncmp(token2, "\"bright\"", 8)) {
              Ps16dz.dimmer = atoi(token3);
              ESP_LOGD(TAG, "PSZ: Brightness %d", Ps16dz.dimmer);

              is_brightness_change = Ps16dz.dimmer != light_dimmer;
              if (power && (Ps16dz.dimmer > 0) && is_brightness_change) {
                light_dimmer =   Ps16dz.dimmer ; 
                id(set_by_touch) = true;    /* Prevent sending the value back to the hardware */
                auto call = dimmer_->turn_on();
                call.set_brightness(light_dimmer / 100.0);
                call.perform();             
              }
            } else if (!strncmp(token2, "\"sequence\"", 10)) {
              ESP_LOGD(TAG, "PSZ: Sequence %s", token3);

            }
            token = strtok_r(nullptr, ",", &end_str);
          }

          //if (!is_brightness_change) {
          ESP_LOGD(TAG, "PSZ: Update");

          SerialSendOkCommand();
        }else if (!strncmp(Ps16dz.rx_buffer+3, "SETTING", 7)) {
            // AT+SETTING=enterESPTOUCH - When ON button is held for over 5 seconds
            // AT+SETTING=exitESPTOUCH  - When ON button is pressed
          SerialSendOkCommand();
        } else if (!strncmp(Ps16dz.rx_buffer+3, "RESULT", 6)) {
          SerialSendOkCommand();
        }
        memset(Ps16dz.rx_buffer, 0, BUFFER_SIZE);
        Ps16dz.byte_counter = 0;
      }
    }
  }
  // Send a serial update command to the LED controller
  // For dimmer types:
  //   AT+UPDATE="sequence":"1554682835320","switch":"on","bright":100
  // For color types:
  //   AT+UPDATE="sequence":"1554682835320","switch":"on","bright":100,"mode":1,"colorR":255,"colorG":46,"colorB":101,"light_types":1
  void SerialSendUpdateCommand(uint8_t light_state_dimmer,bool power)
  {
    light_state_dimmer = (light_state_dimmer < 10) ? 10 : light_state_dimmer;

    snprintf_P(Ps16dz.tx_buffer, BUFFER_SIZE, PSTR("AT+UPDATE=\"sequence\":\"%d%03d\",\"switch\":\"%s\",\"bright\":%d"),
    "", millis()%1000, power?"on":"off", light_state_dimmer);
    SerialSendTxBuffer();
  }

  void PS16DZInit(void)
  {
    
    Ps16dz.tx_buffer = (char*)(malloc(BUFFER_SIZE));
    if (Ps16dz.tx_buffer != nullptr) {
      Ps16dz.rx_buffer = (char*)(malloc(BUFFER_SIZE));
    }
  }


  void setup() override {
    PS16DZInit();

  }




  void loop() override {
    SerialInput();
  }
};
