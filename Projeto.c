#include <stdio.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/rtc.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "hardware/clocks.h"
#include "hardware/adc.h"
#include "hardware/pwm.h"
#include "led.pio.h"
#include "ssd1306.h"

#define LED_PIN_GREEN 11
#define LED_PIN_BLUE 12
#define LED_PIN_RED 13
#define BUTTON_A 5
#define BUTTON_B 6
#define I2C_SDA 14
#define I2C_SCL 15
#define I2C_PORT i2c1
#define JOYSTICK_PB 22 
#define JOYSTICK_X_PIN 26  
#define JOYSTICK_Y_PIN 27  
#define endereco 0x3C
#define BUZZER 21

// Frequências das notas (em Hz)
#define DO 261
#define RE 293
#define MI 329
#define FA 349
#define SOL 392
#define LA 440
#define SI 493
#define DO_OCTAVE 523

PIO pio; // Inicializa a estrutura da PIO
uint sm; // Inicializa a máquina de estados

/*
Matrizes de LEDs

led1: exclamação (!)
led2: certo (✓)
led3: três pontos (...)
*/
double led1[10][25] = {
    {0, 0, 1, 0, 0,
     0, 0, 1, 0, 0,
     0, 0, 1, 0, 0,
     0, 0, 0, 0, 0,
     0, 0, 1, 0, 0,
    }
}; 

double led2[10][25] = {
    {0, 0, 0, 0, 0,
     1, 0, 0, 0, 1,
     0, 1, 0, 1, 0,
     0, 0, 1, 0, 0,
     0, 0, 0, 0, 0,
    }
}; 

double led3[10][25] = {
    {0, 0, 0, 0, 0,
     0, 0, 0, 0, 0,
     0, 1, 1, 1, 0,
     0, 0, 0, 0, 0,
     0, 0, 0, 0, 0,
    }
}; 


// Sequência de LEDs
int sequencia[25] = {
    0, 1, 2, 3, 4,
    9, 8, 7, 6, 5,
    10, 11, 12, 13, 14,
    19, 18, 17, 16, 15,
    20, 21, 22, 23, 24
};


/*
Melhorar a intensidade
cores1: vermelho
cores2: verde
cores3: azul
*/
uint32_t cores1(double vermelho)
{
  unsigned char R;
  R = vermelho * 50; // Ajusta a intensidade do vermelho
  return (R << 16); // Retorna o valor do vermelho (deslocamento de 16 bits) 
}

// Melhorar a intensidade
uint32_t cores2(double verde)
{
  unsigned char G;
  G = verde * 50; // Ajusta a intensidade do verde
  return (G << 24); // Retorna o valor do verde (deslocamento de 24 bits) 
}

// Melhorar a intensidade
uint32_t cores3(double azul)
{
  unsigned char B;
  B = azul * 50; // Ajusta a intensidade do azul
  return (B << 8); // Retorna o valor do azul (deslocamento de 8 bits) 
}

/*
Converte os valores da matriz de LEDs para a matriz de LEDs da PIO

display_num1: exibe a matriz de LEDs led1
display_num2: exibe a matriz de LEDs led2
display_num3: exibe a matriz de LEDs led3
*/
void display_num1(int number) {
    for (int i = 0; i < 25; i++) {
        uint32_t valor_led = cores1(led1[number][sequencia[24 - i]]);
        pio_sm_put_blocking(pio, sm, valor_led);
    }
}

void display_num2(int number) {
    for (int i = 0; i < 25; i++) {
        uint32_t valor_led = cores2(led2[number][sequencia[24 - i]]);
        pio_sm_put_blocking(pio, sm, valor_led);
    }
}

void display_num3(int number) {
    for (int i = 0; i < 25; i++) {
        uint32_t valor_led = cores3(led3[number][sequencia[24 - i]]);
        pio_sm_put_blocking(pio, sm, valor_led);
    }
}

// Função para tocar uma nota no buzzer quando o temporizador chegar a zero
void Tocar_nota(int nota, int duracao_ms) {
    int periodo = 1000000 / nota; // Calcula o período da onda (em microssegundos)
    int ciclos = (nota * duracao_ms) / 1000; // Calcula o número de ciclos a serem tocados
    for (int i = 0; i < ciclos; i++) {
        gpio_put(BUZZER, true);  // Liga o buzzer
        sleep_us(periodo / 2);    // Meio ciclo (high)
        gpio_put(BUZZER, false); // Desliga o buzzer
        sleep_us(periodo / 2);    // Meio ciclo (low)
    }
}
// Função para tocar uma melodia
void Tocar_melodia() {
    // Melodia (sequência de notas e duração em ms)
    int melodia[][2] = {
        {DO, 400}, {RE, 400}, {MI, 400}, {FA, 400},
        {SOL, 400}, {LA, 400}, {SI, 400}, {DO_OCTAVE, 800},
        {SI, 400}, {LA, 400}, {SOL, 400}, {FA, 400},
        {MI, 400}, {RE, 400}, {DO, 800}
    };
    for (int i = 0; i < sizeof(melodia) / sizeof(melodia[0]); i++) {
        int nota = melodia[i][0];
        int duracao = melodia[i][1];
        Tocar_nota(nota, duracao); // Toca cada nota
    }
}

// Variáveis globais
int timer_minutes = 0;
bool timer_active = false;
ssd1306_t display;

// Programa para controlar os LEDs
void set_led_color(int r, int g, int b) {
    gpio_put(LED_PIN_RED, r);
    gpio_put(LED_PIN_GREEN, g);
    gpio_put(LED_PIN_BLUE, b);
}

// Programa para controlar o display OLED
void update_display() {
    char buffer[20];
    sprintf(buffer, "Tempo: %d min", timer_minutes);
    ssd1306_fill(&display, false); // Limpa o display
    ssd1306_draw_string(&display, buffer, 0, 1); // Exibe o tempo restante
    ssd1306_send_data(&display); // Envia os dados para o display
}

// Função para verificar se o temporizador chegou a zero
void check_timer() {
    if (timer_active && timer_minutes == 0) {
        set_led_color(1, 0, 0); // Acende o LED vermelho
        timer_active = false; // Desativa o temporizador
        display_num1(0); // Exibe a matriz de LEDs led1

        Tocar_melodia(); // Toca a melodia no buzzer
    }
}

// Função de interrupção para os botões
void gpio_irq_handler(uint gpio, uint32_t events) {
    uint32_t current_time = to_ms_since_boot(get_absolute_time()); // Obtém o tempo atual
    static uint32_t last_time = 0; // Inicializa a última vez que o botão foi pressionado

    if (current_time - last_time > 200) {
        // Verifica qual botão foi pressionado
        if (gpio == BUTTON_A) {
                timer_minutes += 5;// Adiciona 5 minutos ao temporizador
                set_led_color(0, 0, 1);
                timer_active = true; // Ativa o temporizador
                update_display();
                display_num3(0); // Exibe a matriz de LEDs led3
        } else if (gpio == BUTTON_B) {
            timer_active = false; // Desativa o temporizador
            set_led_color(0, 1, 0); // Acende o LED verde
            display_num2(0); // Exibe a matriz de LEDs led2
        } else if (gpio == JOYSTICK_PB) {
                set_led_color(0, 0, 1); // Acende o LED azul
                timer_active = true; // Ativa o temporizador
                update_display(); // Atualiza o display
                display_num3(0); // Exibe a matriz de LEDs led3           
        }
        last_time = current_time; // Atualiza o tempo do último pressionamento do botão
    }
}

// Função para ler o joystick
void read_joystick() {
    adc_select_input(1); // Seleciona a entrada do ADC
    uint16_t adc_x = adc_read(); // Lê o valor do ADC

    if (adc_x > 3000) {
        timer_minutes += 1; // Se for para direita, adiciona 1 minuto
    } else if (adc_x < 1000) {
        timer_minutes -= 1; // Se for para esquerda, subtrai 1 minuto
        if (timer_minutes < 0) timer_minutes = 0; // Verifica se o temporizador é maior que zero
    }
    update_display();
}

int main() {
    // Inicializa o pico-sdk
    stdio_init_all();

    // Inicializa os pinos
    gpio_init(LED_PIN_RED);
    gpio_set_dir(LED_PIN_RED, GPIO_OUT);
    gpio_init(LED_PIN_BLUE);
    gpio_set_dir(LED_PIN_BLUE, GPIO_OUT);
    gpio_init(LED_PIN_GREEN);
    gpio_set_dir(LED_PIN_GREEN, GPIO_OUT);

    gpio_init(JOYSTICK_PB);
    gpio_set_dir(JOYSTICK_PB, GPIO_IN);
    gpio_pull_up(JOYSTICK_PB);
    gpio_set_irq_enabled_with_callback(JOYSTICK_PB, GPIO_IRQ_EDGE_FALL, true, &gpio_irq_handler);

    gpio_init(BUTTON_A);
    gpio_set_dir(BUTTON_A, GPIO_IN);
    gpio_pull_up(BUTTON_A);
    gpio_set_irq_enabled_with_callback(BUTTON_A, GPIO_IRQ_EDGE_FALL, true, &gpio_irq_handler);

    gpio_init(BUTTON_B);
    gpio_set_dir(BUTTON_B, GPIO_IN);
    gpio_pull_up(BUTTON_B);
    gpio_set_irq_enabled_with_callback(BUTTON_B, GPIO_IRQ_EDGE_FALL, true, &gpio_irq_handler);

    gpio_init(BUZZER);
    gpio_set_dir(BUZZER, GPIO_OUT);


    // Inicializa o display OLED
    adc_init();
    adc_gpio_init(JOYSTICK_X_PIN);

    i2c_init(I2C_PORT, 400 * 1000);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);
    
    ssd1306_init(&display, 128, 64, false, endereco, I2C_PORT);
    ssd1306_config(&display);
    ssd1306_fill(&display, false);
    ssd1306_send_data(&display);
    update_display();

    // Inicializa a PIO
    pio = pio0;
    uint offset = pio_add_program(pio, &led_program); 
    uint sm = pio_claim_unused_sm(pio, true);
    led_program_init(pio, sm, offset, 7);

    while (true) {
        // Verifica se o temporizador está ativo
        if (timer_active && timer_minutes > 0) {
            sleep_ms(60000); 
            timer_minutes--; // Decrementa o temporizador
            update_display();
        }
        // Chamada das funções
        check_timer();
        read_joystick();
        sleep_ms(200);
    }
}
