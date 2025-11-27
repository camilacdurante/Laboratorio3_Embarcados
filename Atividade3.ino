// Bibliotecas
#include <semphr.h>
#include <LiquidCrystal.h>
#include <Arduino_FreeRTOS.h>

// Pinos do LCD (RS, E, D4, D5, D6, D7)
const int rs = 8, e = 9, d4 = 10, d5 = 11, d6 = 12, d7 = 13;
LiquidCrystal lcd(rs, e, d4, d5, d6, d7);

// Pinos do joystick (e botao integrado) e do buzzer
const int pinJoystickX = A0;     // Eixo X do joystick (analogico)
const int pinJoystickY = A1;     // Eixo Y do joystick (analogico)
const int pinJoystickButton = 2; // Botao do joystick (digital)
const int pinBuzzer = 4;         // Buzzer (para tocar o alarme)

// Estrutura de tempo (horas, minutos, segundos)
typedef struct {
  uint8_t horas, minutos, segundos;
} Tempo;

// Variaveis de tempo compartilhadas entre tasks (declared volatile por seguranca)
volatile Tempo horarioAtual = {10, 0, 0};  // horario corrente inicial
volatile Tempo horarioAlarme = {10, 1, 0}; // horario do alarme inicial

// Estados principais da maquina de estados (mantidos do Lab 2)
enum EstadoPrincipal {ESPERA, AJUSTA_HORA, AJUSTA_ALARME, TOCANDO};
volatile EstadoPrincipal estadoAtual = ESPERA; // estado inicial

// Prototipos das funcoes / tasks
void displayTimeLabel(const char* label, volatile const Tempo& t, uint8_t row);
void taskDisplay(void* parametros);
void taskJoystick(void* parametros);
void taskAlarme(void* parametros);

// Configuracoes iniciais e criacao das tasks ---
void setup() {
  pinMode(pinJoystickButton, INPUT_PULLUP); // botao com pull-up interno
  pinMode(pinBuzzer, OUTPUT);               // buzzer como saida
  lcd.begin(16, 2);                         // inicializa o LCD 16x2

  // Cria as tasks do FreeRTOS
  xTaskCreate(taskDisplay, "Display", 128, NULL, 2, NULL); // task responsavel pelo LCD e "relogio"
  xTaskCreate(taskJoystick, "Joystick", 128, NULL, 1, NULL); // task que le o joystick e altera estados
  xTaskCreate(taskAlarme, "Alarm", 128, NULL, 1, NULL); // task que verifica e toca alarme

  // Inicia o escalonador do FreeRTOS (nunca retorna se OK)
  vTaskStartScheduler();
}

void loop() {
  // Vazio, pois toda a execucao e controlada pelo FreeRTOS
}

// Funcao auxiliar: imprime rotulo + tempo formatado na linha especificada do LCD
void displayTimeLabel(const char* label, volatile const Tempo& t, uint8_t row) {
  lcd.setCursor(0, row);
  lcd.print(label);
  // Adiciona zero a esquerda quando necessario (formato HH:MM:SS)
  if (t.horas < 10) lcd.print('0');
  lcd.print(t.horas);
  lcd.print(':');
  if (t.minutos < 10) lcd.print('0');
  lcd.print(t.minutos);
  lcd.print(':');
  if (t.segundos < 10) lcd.print('0');
  lcd.print(t.segundos);
}

// Funcao taskDisplay atualiza o LCD e avanca o relogio a cada 1 segundo
void taskDisplay(void* parametros) {
  (void) parametros;
  for (;;) {
    // Se o alarme esta tocando, mostra mensagem dedicada
    if (estadoAtual == TOCANDO) {
      lcd.clear();
      lcd.setCursor(0,0);
      lcd.print("ALARME TOCANDO");
    } else {
      // Senao, mostra horario atual e horario do alarme
      lcd.clear();
      displayTimeLabel("Hora: ", horarioAtual, 0);
      displayTimeLabel("Alarme:", horarioAlarme, 1);
    }

    // Aguarda 1 segundo usando FreeRTOS (delay deterministico)
    vTaskDelay(pdMS_TO_TICKS(1000));

    // Atualiza o relogio (incrementa segundos e faz o rollover)
    horarioAtual.segundos++;
    if (horarioAtual.segundos >= 60) {
      horarioAtual.segundos = 0;
      horarioAtual.minutos++;
      if (horarioAtual.minutos >= 60) {
        horarioAtual.minutos = 0;
        horarioAtual.horas = (horarioAtual.horas + 1) % 24;
      }
    }
  }
}

// Funcao taskJoystick le o joystick e o botao, altera estados e ajusta hora/alarme
void taskJoystick(void* parametros) {
  (void) parametros;
  bool foiPressionado = false; // para detectar borda de pressao do botao
  for (;;) {
    bool pressionado = (digitalRead(pinJoystickButton) == LOW); // LOW = pressionado com INPUT_PULLUP
    // Deteccao de borda de descida do botao
    if (pressionado && !foiPressionado) {
      foiPressionado = true;
      // Ciclo de estados: ESPERA -> AJUSTA_HORA -> AJUSTA_ALARME -> ESPERA
      if (estadoAtual == ESPERA) {
        estadoAtual = AJUSTA_HORA;
      } else if (estadoAtual == AJUSTA_HORA) {
        estadoAtual = AJUSTA_ALARME;
      } else if (estadoAtual == AJUSTA_ALARME) {
        // Quando termina ajuste do alarme, confirma e da um feedback sonoro curto
        estadoAtual = ESPERA;
        digitalWrite(pinBuzzer, HIGH);
        tone(pinBuzzer, 100);
        vTaskDelay(pdMS_TO_TICKS(200));
        digitalWrite(pinBuzzer, LOW);
        noTone(pinBuzzer);
        lcd.clear();
        lcd.setCursor(0,0);
        lcd.print("Alarme definido: ");
        lcd.setCursor(0,1);
        if (horarioAlarme.horas < 10) lcd.print('0');
        lcd.print(horarioAlarme.horas);
        lcd.print(':');
        if (horarioAlarme.minutos < 10) lcd.print('0');
        lcd.print(horarioAlarme.minutos);
        vTaskDelay(pdMS_TO_TICKS(2000));
        lcd.clear();
      } else if (estadoAtual == TOCANDO) {
        // Se o alarme estiver tocando, o botao interrompe o alarme e volta para o estado de ESPERA
        estadoAtual = ESPERA;
        digitalWrite(pinBuzzer, LOW);
        noTone(pinBuzzer);
      }
    } else if (!pressionado && foiPressionado) {
      // Reset da flag quando o botao e solto
      foiPressionado = false;
    }

    // Se estiver em modo de ajuste, le os eixos para ajustar horas/minutos
    if (estadoAtual == AJUSTA_HORA || estadoAtual == AJUSTA_ALARME) {
      int x = analogRead(pinJoystickX);
      int y = analogRead(pinJoystickY);
      // Ajuste de horas pelo eixo X
      if (x < 200) {
        if (estadoAtual == AJUSTA_HORA)
          horarioAtual.horas = (horarioAtual.horas + 23) % 24; // decrementa com wrap-around
        else
          horarioAlarme.horas = (horarioAlarme.horas + 23) % 24;
        vTaskDelay(pdMS_TO_TICKS(200)); // debounce/protecao contra repeticoes muito rapidas
      } else if (x > 800) {
        if (estadoAtual == AJUSTA_HORA)
          horarioAtual.horas = (horarioAtual.horas + 1) % 24; // incrementa com wrap-around
        else
          horarioAlarme.horas = (horarioAlarme.horas + 1) % 24;
        vTaskDelay(pdMS_TO_TICKS(200));
      }
      // Ajuste de minutos pelo eixo Y
      else if (y < 200) {
        if (estadoAtual == AJUSTA_HORA)
          horarioAtual.minutos = (horarioAtual.minutos + 1) % 60;
        else
          horarioAlarme.minutos = (horarioAlarme.minutos + 1) % 60;
        vTaskDelay(pdMS_TO_TICKS(200));
      } else if (y > 800) {
        if (estadoAtual == AJUSTA_HORA)
          horarioAtual.minutos = (horarioAtual.minutos + 59) % 60; // decrementa com wrap
        else
          horarioAlarme.minutos = (horarioAlarme.minutos + 59) % 60;
        vTaskDelay(pdMS_TO_TICKS(200));
      }
    }

    // Pequena espera para reduzir uso de CPU
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

// Funcao taskAlarme verifica coincidencia de horario e controla o buzzer quando disparado
void taskAlarme(void* parametros) {
  (void) parametros;
  static bool alarmeAtivo = false; // flag local para evitar retrigger continuo
  for (;;) {
    // Verifica se chegou a hora exata do alarme (HH:MM:SS)
    if (!alarmeAtivo && horarioAtual.horas == horarioAlarme.horas && horarioAtual.minutos == horarioAlarme.minutos && horarioAtual.segundos == horarioAlarme.segundos) {
      alarmeAtivo = true;   // marca que o alarme ja foi acionado
      estadoAtual = TOCANDO; // muda o estado global para TOCANDO
    }

    if (estadoAtual == TOCANDO) {
      // Comportamento de alarme: toca por 10s e pausa 30s (loop enquanto estiver em TOCANDO)
      digitalWrite(pinBuzzer, HIGH);
      tone(pinBuzzer, 100);
      vTaskDelay(pdMS_TO_TICKS(10000)); // mantem o som por 10 segundos
      digitalWrite(pinBuzzer, LOW);
      noTone(pinBuzzer);
      vTaskDelay(pdMS_TO_TICKS(30000)); // pausa de 30 segundos entre toques
    } else {
      // Se o estado voltar para ESPERA apos ter tocado, reseta a flag
      if (alarmeAtivo && estadoAtual == ESPERA) {
        alarmeAtivo = false;
      }
      vTaskDelay(pdMS_TO_TICKS(500)); // espera curta quando nao esta tocando
    }
  }
}
