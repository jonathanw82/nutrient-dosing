#include <Arduino.h>
#include <avr/wdt.h>

#include <bridge_device.h>
#include <dosa.h>
#include <utils.h>

#define ON true
#define OFF false

#define DOSE_TIME_MS (this->ph_dose_time_s * 1000)

const char *Dosa = "dosa";
int dosa_instance_count = 0;

Dosa_Cls::Dosa_Cls() {

    this->instance_number = dosa_instance_count;
    dosa_instance_count += 1;

    this->lockout_led_pin = 0;
    this->mixture_valve_pin = 0;
    this->mixture_valve_pin_state = false;
    this->ph_valve_pin = 0;
    this->ph_valve_pin_state = false;
    this->nutrient_A_valve_pin = 0;
    this->nutrient_A_valve_pin_state = false;
    this->nutrient_B_valve_pin = 0;
    this->nutrient_B_valve_pin_state = false;

    this->emergency_stop_pin = 0;
    this->emergency_stop_state = true;

    this->flow_rate = 0;
    this->ratio_of_A_to_B = 0;
    this->prev_ratio_of_A_to_B = 0;
    this->needs_to_dose_ec = false;
    this->needs_to_dose_ph = false;
    this->mixture_state = false;
    this->current_mixture_state = false;
    this->ph_dose_time_s = 0;
    this->dose_ph_timer = 0;

    this->dose_A_timer = 0;
    this->dose_A_time_s = 0;
    this->prev_dose_A_time_s = 0;
    this->dose_B_timer = 0;
    this->dose_B_time_s = 0;
    this->prev_dose_B_time_s = 0;
    this->ph_dose_state = ph_dose_idle;
    this->ec_A_dose_state = ec_dose_idle;
    this->ec_B_dose_state = ec_dose_idle;
    this->dose_lockout = false;
    this->prev_lockout_state_control = false;
    this->prev_lockout_state_ec = false;
    this->prev_lockout_state_ph = false;
    this->prev_emergency_stop_state = false;
    this->safety_timout_limit_s = 120000;
    this->lockout_type = none_lockout;
}

void Dosa_Cls::init() {
    wdt_reset();

    char path[MAX_PATH_LENGTH];
    this->get_commission_path_str(path);

    // led pins
    if (this->lockout_led_pin == 0) {
        Serial.print(path);
        Serial.println(F(": lockout led pin not set"));
    }
    digitalWrite(this->lockout_led_pin, OFF);
    pinMode(this->lockout_led_pin, OUTPUT);

    // valve pins
    if (this->mixture_valve_pin == 0) {
        Serial.print(path);
        Serial.println(F(": dosa mixture valve pin not set"));
    }
    digitalWrite(this->mixture_valve_pin, OFF);
    pinMode(this->mixture_valve_pin, OUTPUT);

    if (this->ph_valve_pin == 0) {
        Serial.print(path);
        Serial.println(F(": ph valve pin not set"));
    }
    digitalWrite(this->ph_valve_pin, OFF);
    pinMode(this->ph_valve_pin, OUTPUT);

    if (this->nutrient_A_valve_pin == 0) {
        Serial.print(path);
        Serial.println(F(": nutrient A valve pin not set"));
    }
    digitalWrite(this->nutrient_A_valve_pin, OFF);
    pinMode(this->nutrient_A_valve_pin, OUTPUT);

    if (this->nutrient_B_valve_pin == 0) {
        Serial.print(path);
        Serial.println(F(": nutrient B valve pin not set"));
    }
    digitalWrite(this->nutrient_B_valve_pin, OFF);
    pinMode(this->nutrient_B_valve_pin, OUTPUT);

    if (this->emergency_stop_pin == 0) {
        Serial.print(path);
        Serial.println(F(": emergency stop pin not set"));
    }
    digitalWrite(this->emergency_stop_pin, OFF);
    pinMode(this->emergency_stop_pin, INPUT);

    this->device->add_module_to_list(this);

    Serial.print(F("Device: "));
    Serial.print(path);
    Serial.println(F(" initialised"));
}

void Dosa_Cls::get_commission_path_str(char *path) {
    mac_str(path, this->device->mac_address);
    strcat(path, "/");
    strcat(path, String(this->instance_number).c_str());
    strcat(path, "/");
    strcat(path, Dosa);
    strcat(path, "/");
}

void Dosa_Cls::run_uncommissioned_state() {
    if (!this->device->mqtt_connected) {
        return;
    }
    if (millis() - this->commission_publish_timer > 60000) {
        this->commissioning_subscribe();
        this->publish_commissioning_type();
        this->commission_publish_timer = millis();
    }
}

bool Dosa_Cls::process_message(char *topic, char *payload) {
    // path and reset messages
    if (this->process_module_messages(topic, payload)) {
        return true;
    }
    if (this->topic_main_path_match(topic, FStr(F("control/flow-rate-lpm")))) {
        return parse_float_from_string(payload, &this->flow_rate);
    }
    if (this->topic_main_path_match(topic, FStr(F("control/ratio-of-A-to-B-%")))) {
        return parse_float_from_string(payload, &this->ratio_of_A_to_B);
    }
    if (this->topic_main_path_match(topic, FStr(F("control/ec-dose")))) {
        return parse_bool_from_char(payload, &this->needs_to_dose_ec);
    }
    if (this->topic_main_path_match(topic, FStr(F("control/ph-dose")))) {
        return parse_bool_from_char(payload, &this->needs_to_dose_ph);
    }
    if (this->topic_main_path_match(topic, FStr(F("control/run-mixture")))) {
        return parse_bool_from_char(payload, &this->mixture_state);
    }
    if (this->topic_main_path_match(topic, FStr(F("control/ph-dose-time-s")))) {
        return parse_ul_from_string(payload, &this->ph_dose_time_s);
    }
    if (this->topic_main_path_match(topic, FStr(F("control/dose-lockout")))) {
        this->lockout_type = safety_dose_lockout;
        return parse_bool_from_char(payload, &this->dose_lockout);
    }
    return false;
}

void Dosa_Cls::manage_lockout() {

    if (!this->dose_lockout) {
        return;
    }

    this->ph_dose_state = ph_dose_end;
    this->ec_A_dose_state = ec_dose_end;
    this->ec_B_dose_state = ec_dose_end;

    this->device->set_pin(this->lockout_led_pin, ON);

    if (this->lockout_type == safety_dose_lockout) {
        this->lockout_state_control = true;
        this->pub_stat_dose_lockout();
    }
    if (this->lockout_type == safety_timer_lockout_PH) {
        this->lockout_state_ph = true;
        this->pub_stat_dose_lockout_PH();
    }
    if (this->lockout_type == safety_timer_lockout_EC) {
        this->lockout_state_ec = true;
        this->pub_stat_dose_lockout_EC();
    }
    if(this->lockout_type == emergency_stop_button){
        this->emergency_stop_state = true;
        this->pub_stat_emergency_stop();
    }
}

void Dosa_Cls::manage_emergency_stop() {
    // this gives a status but does not physically do anything
    this->emergency_stop_state = !digitalRead(this->emergency_stop_pin);

    if (this->emergency_stop_state != this->current_emergency_stop_state) {
        this->current_emergency_stop_state = this->emergency_stop_state;
        this->dose_lockout;
        this->lockout_type = emergency_stop_button;
    }
}

bool Dosa_Cls::manage_mixture() {

    if (this->mixture_state != this->current_mixture_state) {
        this->current_mixture_state = this->mixture_state;
        this->device->set_pin(this->mixture_valve_pin, this->current_mixture_state);
        this->mixture_valve_pin_state = this->current_mixture_state;
        this->pub_stat_mixture();
    }
    return true;
}

bool Dosa_Cls::calculate_ec_ratio() {
    /*
        This funtion takes the dose amount, say 1 litre and divides it with the flowrate, this gives total
        valve on time. This time can then be used to calculate the ratio between A and B.
    */
    if (this->flow_rate <= 0) {
        return;
    }

    if (this->ratio_of_A_to_B != this->prev_ratio_of_A_to_B) {
        if (this->ratio_of_A_to_B >= 100) {
            this->ratio_of_A_to_B = 100;
        }
        if (this->ratio_of_A_to_B <= 0) {
            this->ratio_of_A_to_B = 0;
        }

        this->prev_ratio_of_A_to_B = this->ratio_of_A_to_B;

        float total_valve_on_time = this->dose_amount_l / this->flow_rate;

        float A_ratio_as_decimal = this->ratio_of_A_to_B / 100.0;

        float b_ratio_as_decimal = (100 - this->ratio_of_A_to_B) / 100.0;

        this->dose_A_time_s = ((A_ratio_as_decimal * total_valve_on_time) * 60.0) * 1000;
        this->dose_B_time_s = ((b_ratio_as_decimal * total_valve_on_time) * 60.0) * 1000;

        this->pub_doseing_times();

        return true;
    }
}

bool Dosa_Cls::check_ec_A_safety_timer() {
    /*
        Check the pin state and start the timer, if the pin does not become false before the safety timer
        has elappsed, then trigger the safety time out, to stop over dosing.
    */

    if (this->nutrient_A_valve_pin_state) {
        if (millis() - this->dose_A_timer < this->safety_timout_limit_s) {
            return;
        }
        this->lockout_state_ec = true;
        this->dose_lockout = true;
        this->lockout_type = safety_timer_lockout_EC;
        this->needs_to_dose_ec = false;
    }
    return true;
}

bool Dosa_Cls::check_ec_B_safety_timer() {

    if (this->nutrient_B_valve_pin_state) {
        if (millis() - this->dose_B_timer < this->safety_timout_limit_s) {
            return;
        }
        this->lockout_state_ec = true;
        this->dose_lockout = true;
        this->lockout_type = safety_timer_lockout_EC;
        this->needs_to_dose_ec = false;
    }
    return true;
}

bool Dosa_Cls::check_ph_safety_timer() {

    if (this->ph_valve_pin_state) {
        if (millis() - this->dose_ph_timer < this->safety_timout_limit_s) {
            return;
        }
        this->lockout_state_ph = true;
        this->dose_lockout = true;
        this->lockout_type = safety_timer_lockout_PH;
        this->needs_to_dose_ph = false;
    }
    return true;
}

bool Dosa_Cls::dose_nutrient_a() {

    if (this->ratio_of_A_to_B < 1) {
        return;
    }

    switch (this->ec_A_dose_state) {

        case ec_dose_idle:
            if (!this->needs_to_dose_ec) {
                return;
            }
            if (!this->dose_lockout) {
                this->ec_A_dose_state = ec_dose_start;
            }
            break;

        case ec_dose_start:
            this->device->set_pin(this->nutrient_A_valve_pin, ON); // make functions for speaerte pins
            this->nutrient_A_valve_pin_state = true;
            this->dose_A_timer = millis();
            this->pub_stat_nutrient_a();
            this->ec_A_dose_state = ec_dose_run_timer;
            break;

        case ec_dose_run_timer:
            if (millis() - this->dose_A_timer <= this->dose_A_time_s) {
                return;
            }
            this->dose_A_timer = millis();
            this->ec_A_dose_state = ec_dose_end;
            break;

        case ec_dose_end:
            this->device->set_pin(this->nutrient_A_valve_pin, OFF);
            this->nutrient_A_valve_pin_state = false;
            this->needs_to_dose_ec = false;
            this->pub_stat_nutrient_a();
            this->ec_A_dose_state = ec_dose_idle;
            break;
        default:
            this->ec_A_dose_state = ec_dose_idle;
            break;
    }
    return true;
}

bool Dosa_Cls::dose_nutrient_b() {

    if (this->ratio_of_A_to_B < 1) {
        return;
    }

    switch (this->ec_B_dose_state) {

        case ec_dose_idle:
            if (!this->needs_to_dose_ec) {
                return;
            }
            if (!this->dose_lockout) {
                this->ec_B_dose_state = ec_dose_start;
            }
            break;

        case ec_dose_start:
            this->device->set_pin(this->nutrient_B_valve_pin, ON);
            this->nutrient_B_valve_pin_state = true;
            this->dose_B_timer = millis();
            this->pub_stat_nutrient_b();
            this->ec_B_dose_state = ec_dose_run_timer;
            break;

        case ec_dose_run_timer:
            if (millis() - this->dose_B_timer <= this->dose_B_time_s) {
                return;
            }
            this->dose_B_timer = millis();
            this->ec_B_dose_state = ec_dose_end;
            break;

        case ec_dose_end:
            this->device->set_pin(this->nutrient_B_valve_pin, OFF);
            this->nutrient_B_valve_pin_state = false;
            this->needs_to_dose_ec = false;
            this->pub_stat_nutrient_b();
            this->ec_B_dose_state = ec_dose_idle;
            break;

        default:
            this->ec_B_dose_state = ec_dose_idle;
            break;
    }
    return true;
}

bool Dosa_Cls::dose_ph() {

    wdt_reset();

    if (this->ph_dose_time_s < 1) {
        return;
    }

    switch (ph_dose_state) {

        case ph_dose_idle:
            if (!this->needs_to_dose_ph) {
                return;
            }
            if (!this->dose_lockout) {
                this->ph_dose_state = ph_dose_start;
            }
            break;

        case ph_dose_start:
            this->device->set_pin(this->ph_valve_pin, ON);
            this->ph_valve_pin_state = true;
            this->dose_ph_timer = millis();
            this->pub_stat_ph();
            this->ph_dose_state = ph_dose_run_timer;
            break;

        case ph_dose_run_timer:
            if (millis() - this->dose_ph_timer < DOSE_TIME_MS) {
                return;
            }
            this->dose_ph_timer = millis();
            this->ph_dose_state = ph_dose_end;
            break;

        case ph_dose_end:
            this->device->set_pin(this->ph_valve_pin, OFF);
            this->ph_valve_pin_state = false;
            this->needs_to_dose_ph = false;
            this->pub_stat_ph();
            this->ph_dose_state = ph_dose_idle;
            break;

        default:
            this->ph_dose_state = ph_dose_idle;
            break;
    }
    return true;
}

void Dosa_Cls::publish_status() {

    wdt_reset();
    char val[MAX_PATH_LENGTH];

    mac_str(val, this->device->mac_address);
    this->publish_main(FStr(F("mac-address")), val, true, 1);
    strcpy(val, Dosa);
    this->publish_main(FStr(F("firmware-type")), val, true, 1);

    // Hardware endpoints
    this->publish_main(FStr(F("hardware/ph-pin")), this->ph_valve_pin, true, 1);
    this->publish_main(FStr(F("hardware/mixture-pin")), this->mixture_valve_pin, true, 1);
    this->publish_main(FStr(F("hardware/nutrient-a-pin")), this->nutrient_A_valve_pin, true, 1);
    this->publish_main(FStr(F("hardware/nutrient-b-pin")), this->nutrient_B_valve_pin, true, 1);
    this->publish_main(FStr(F("hardware/emergency-stop-pin")), this->emergency_stop_pin, true, 1);

    // control status end points
    this->publish_main(FStr(F("control/flow-rate-lpm")), this->flow_rate, true, 1);
    this->publish_main(FStr(F("control/ratio-of-A-to-B-%")), this->ratio_of_A_to_B, true, 1);
    this->publish_main(FStr(F("control/ec-dose")), this->needs_to_dose_ec, false, 1);
    this->publish_main(FStr(F("control/ph-dose")), this->needs_to_dose_ph, false, 1);
    this->publish_main(FStr(F("control/run-mixture")), this->mixture_state, false, 1);
    this->publish_main(FStr(F("control/ph-dose-time-s")), this->ph_dose_time_s, true, 1);
    this->publish_main(FStr(F("control/dose-lockout")), this->dose_lockout, true, 1);

    // status-tres")), this->dose_amount_l, true, 1);
    this->publish_main(FStr(F("status/ph-pin")), this->ph_valve_pin_state, false, 1);
    this->publish_main(FStr(F("status/mixture-pin")), this->mixture_valve_pin_state, false, 1);
    this->publish_main(FStr(F("status/nutrient-A-dosing-time-s")), this->dose_A_time_s, false, 1);
    this->publish_main(FStr(F("status/nutrient-B-dosing-time-s")), this->dose_B_time_s, false, 1);
    this->publish_main(FStr(F("status/nutrient-A-valve-pin")), this->nutrient_A_valve_pin_state, false, 1);
    this->publish_main(FStr(F("status/nutrient-B-valve-pin")), this->nutrient_B_valve_pin_state, false, 1);
    this->publish_main(FStr(F("status/doser-lockout")), this->dose_lockout, false, 1);
    this->publish_main(FStr(F("status/safety-timer-lockout-ph")), this->dose_lockout, false, 1);
    this->publish_main(FStr(F("status/safety-timer-lockout-ec")), this->dose_lockout, false, 1);
    this->publish_main(FStr(F("status/emergency-stop-button")), this->emergency_stop_state, false, 1);
}

void Dosa_Cls::pub_stat_ph() {
    if (this->ph_valve_pin_state != this->prev_ph_valve_pin_state) {
        this->publish_main(FStr(F("status/ph-pin")), this->ph_valve_pin_state, false, 1);
        this->prev_ph_valve_pin_state = this->ph_valve_pin_state;
    }
}

void Dosa_Cls::pub_stat_mixture() {
    if (this->mixture_valve_pin_state != this->prev_mixture_valve_pin_state) {
        this->publish_main(FStr(F("status/mixture-pin")), this->mixture_valve_pin_state, false, 1);
        this->prev_mixture_valve_pin_state = this->mixture_valve_pin_state;
    }
}

void Dosa_Cls::pub_doseing_times() {

    if (this->dose_A_time_s != this->prev_dose_A_time_s) {
        this->publish_main(FStr(F("status/nutrient-A-dosing-time-s")), this->dose_A_time_s, false, 1);
        this->prev_dose_A_time_s = this->dose_A_time_s;
    }

    if (this->dose_B_time_s != this->prev_dose_B_time_s) {
        this->publish_main(FStr(F("status/nutrient-B-dosing-time-s")), this->dose_B_time_s, false, 1);
        this->prev_dose_B_time_s = this->dose_B_time_s;
    }
}

void Dosa_Cls::pub_stat_nutrient_a() {
    if (this->nutrient_A_valve_pin_state != this->prev_nutrient_A_valve_pin_state) {
        this->publish_main(FStr(F("status/nutrient-A-valve-pin")), this->nutrient_A_valve_pin_state, false, 1);
        this->prev_nutrient_A_valve_pin_state = this->nutrient_A_valve_pin_state;
    }
}

void Dosa_Cls::pub_stat_nutrient_b() {
    if (this->nutrient_B_valve_pin_state != this->prev_nutrient_B_valve_pin_state) {
        this->publish_main(FStr(F("status/nutrient-B-valve-pin")), this->nutrient_B_valve_pin_state, false, 1);
        this->prev_nutrient_B_valve_pin_state = this->nutrient_B_valve_pin_state;
    }
}

void Dosa_Cls::pub_stat_emergency_stop() {
    if (this->emergency_stop_state != this->prev_emergency_stop_state) {
        this->publish_main(FStr(F("status/emergency-stop-button")), this->emergency_stop_state, false, 1);
        this->prev_emergency_stop_state = this->emergency_stop_state;
    }
}

void Dosa_Cls::pub_stat_dose_lockout() {
    if(this->lockout_state_control != this->prev_lockout_state_control){
        this->publish_main(FStr(F("status/doser-lockout")), this->dose_lockout, false, 1);
        this->prev_lockout_state_control = this->lockout_state_control;
    }
}

void Dosa_Cls::pub_stat_dose_lockout_EC() {
    if(this->lockout_state_ec != this->prev_lockout_state_ec){
        this->publish_main(FStr(F("status/doser-safety-timer-lockout-ec")), this->dose_lockout, false, 1);
        this->prev_lockout_state_ec = this->lockout_state_ec;
    }
}

void Dosa_Cls::pub_stat_dose_lockout_PH() {
    if(this->lockout_state_ph != this->prev_lockout_state_ph){
        this->publish_main(FStr(F("status/doser-safety-timer-lockout-ph")), this->dose_lockout, false, 1);
        this->prev_lockout_state_ph = this->lockout_state_ph;
    }
}

void Dosa_Cls::check_error_state() {
    this->check_ec_A_safety_timer();
    this->check_ec_B_safety_timer();
    this->check_ph_safety_timer();
    this->manage_lockout();
    this->manage_emergency_stop();
}

void Dosa_Cls::main() {

    wdt_reset();

    if (!this->commissioned) {
        this->run_uncommissioned_state();
        return;
    }

    if (this->device->new_control_connection) {
        this->publish_status();
    }

    if (this->device->new_mqtt_connection || this->newly_commissioned) {
        this->commissioning_subscribe();
        this->control_subscribe();
        this->publish_status();

        this->newly_commissioned = false;
    }

    this->check_error_state();
    this->calculate_ec_ratio();
    this->dose_nutrient_a();
    this->dose_nutrient_b();
    this->dose_ph();
    this->manage_mixture();
}