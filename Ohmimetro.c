// ---------------- Bibliotecas - Início ----------------

// Biblioteca padrão de entrada e saída do C (Foi usada para debugging)
#include <stdio.h>
#include <math.h>

// Bibliotecas do pico SDK de mais alto nível
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "pico/bootrom.h" // PARA TESTES

// Bibliotecas do pico SDK de hardware
#include "hardware/i2c.h"
#include "hardware/pio.h"
#include "hardware/adc.h"
#include "hardware/clocks.h"

#include "inc/ssd1306.h" // Header para controle do display OLED
#include "inc/font.h"    // Header para a fonte do display OLED

#include "ws2812.pio.h"  // Header para controle dos LEDs WS2812

// ---------------- Bibliotecas - Fim ----------------



// ---------------- Definições - Início ----------------

// Configurações do I2C para comunicação com o display OLED
#define I2C_PORT i2c1 // Porta I2C
#define I2C_SDA 14    // Pino de dados
#define I2C_SCL 15    // Pino de clock
#define ADDRESS 0x3C  // Endereço do display

// Definições da matriz de LEDs
#define LED_COUNT 25  // Número total de LEDs na matriz
#define MATRIX_PIN 7  // Pino da matriz de LEDs
struct pixel_t {   // Estrutura para armazenar as cores de um LED WS2812
  uint8_t G, R, B; // Componentes de cor (verde, vermelho e azul)
};
typedef struct pixel_t pixel_t;
typedef pixel_t npLED_t;
npLED_t leds[LED_COUNT];
PIO np_pio; // Instância do PIO
uint sm;    // State machine para controle dos LEDs

// Configuração do botão
#define BUTTON_B 6 // Pino do botão B

// Configuração para o ohmímetro
#define ADC_PIN 28          // GPIO de leitura
#define ADC_VREF 3.30f       // Tensão de referência do ADC
#define ADC_RESOLUTION 4095 // Resolução do ADC (12 bits)
#define R_CONHECIDO 9920    // Resistor conhecido

typedef struct {
    uint8_t R, G, B;
}Color;

Color resistor_colors[10] = {
    {0, 0, 0},       // 0 - Preto
    {188, 32, 0},    // 1 - Marrom
    {188, 0, 0},     // 2 - Vermelho
    {255, 200, 0},   // 3 - Laranja
    {235, 230, 0},   // 4 - Amarelo
    {0, 188, 0},     // 5 - Verde
    {0, 0, 188},     // 6 - Azul
    {130, 0, 240},   // 7 - Violeta 
    {128, 128, 128}, // 8 - Cinza
    {188, 188, 188}  // 9 - Branco
};

static const char *nome_cores[10] = {
    "pret", // 0 - Preto
    "marr", // 1 - Marrom
    "verm", // 2 - Vermelho
    "lara", // 3 - Laranja
    "amar", // 4 - Amarelo
    "verd", // 5 - Verde
    "azul", // 6 - Azul
    "viol", // 7 - Violeta
    "cinz", // 8 - Cinza
    "bran"  // 9 - Branco
};

// ---------------- Definições - Fim ----------------



// ---------------- Variáveis - Início ----------------

static volatile uint32_t last_time = 0; // Armazena o último tempo registrado nas interrupções

// ---------------- Variáveis - Fim ----------------



// ---------------- Inicializações - Início ----------------

// Inicializa o display OLED via I2C
void init_display(ssd1306_t *ssd) {
    i2c_init(I2C_PORT, 400 * 1000); // Inicializa o I2C com frequência de 400 kHz

    // Configura os pinos SDA e SCL como I2C e habilita pull-ups
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);

    // Inicializa e configura o display
    ssd1306_init(ssd, WIDTH, HEIGHT, false, ADDRESS, I2C_PORT);
    ssd1306_config(ssd);
    ssd1306_send_data(ssd);
    
    // Limpa o display
    ssd1306_fill(ssd, false);
    ssd1306_send_data(ssd);
}

// Inicializa o botão B
void init_button() {
    gpio_init(BUTTON_B);
    gpio_set_dir(BUTTON_B, GPIO_IN);
    gpio_pull_up(BUTTON_B);
}

// -------- Matriz - Início --------

// Inicializa a máquina PIO para controle da matriz de LEDs
void npInit(uint pin) {

    // Carrega o programa PIO para controle dos LEDs
    uint offset = pio_add_program(pio0, &ws2812_program);
    np_pio = pio0;

    // Obtém uma máquina de estado PIO disponível
    sm = pio_claim_unused_sm(np_pio, false);
    if (sm < 0) {
        np_pio = pio1;
        sm = pio_claim_unused_sm(np_pio, true);
    }

    // Inicializa a máquina de estado com o WS2812.pio
    ws2812_program_init(np_pio, sm, offset, pin, 800000.f);

    // Limpa o buffer de pixels
    for (uint i = 0; i < LED_COUNT; ++i) {
        leds[i].R = 0;
        leds[i].G = 0;
        leds[i].B = 0;
    }
}

// Atribui uma cor RGB a um LED específico na matriz
void npSetLED(const uint index, const uint8_t r, const uint8_t g, const uint8_t b) {
    leds[index].R = r;
    leds[index].G = g;
    leds[index].B = b;
}

// Limpa todos os LEDs na matriz
void npClear() {
    for (uint i = 0; i < LED_COUNT; ++i) {
        npSetLED(i, 0, 0, 0);
    }
}

// Escreve os dados do buffer para os LEDs
void npWrite() {
    // Escreve cada dado de 8 bits dos pixels em sequência no buffer da máquina PIO
    for (uint i = 0; i < LED_COUNT; ++i) {
        pio_sm_put_blocking(np_pio, sm, leds[i].G);
        pio_sm_put_blocking(np_pio, sm, leds[i].R);
        pio_sm_put_blocking(np_pio, sm, leds[i].B);
    }
    sleep_us(100); // Espera 100us para o reset
}

// Função para facilitar o desenho na matriz utilizando 3 matrizes cos os valores RGB
void npDraw(uint8_t vetorR[5][5], uint8_t vetorG[5][5], uint8_t vetorB[5][5]) {
  int i, j,idx,col;
    for (i = 0; i < 5; i++) {
        idx = (4 - i) * 5; // Calcula o índice base para a linha
        for (j = 0; j < 5; j++) {
            col = (i % 2 == 0) ? (4 - j) : j; // Inverte a ordem das colunas nas linhas pares
            npSetLED(idx + col, vetorR[i][j], vetorG[i][j], vetorB[i][j]); // Preenche o buffer com os valores das matrizes
        }
    }
}

// -------- Matriz - Fim --------

// ---------------- Inicializações - Fim ----------------



// -------- Callback - Início --------

// Função de callback para tratar as interrupções dos botões
void gpio_irq_callback(uint gpio, uint32_t events) {
    uint32_t current_time = to_ms_since_boot(get_absolute_time()); // Obtém o tempo atual em ms

    // Debounce de 200 ms
    if( (current_time - last_time) > 200 ) {
        if(gpio == BUTTON_B) {
            reset_usb_boot(0, 0);
        }
    }
}

// -------- Callback - Fim --------

// Função para calcular a resistência com base nos valores lidos do ADC do pino 28
int ler_resistor(float *r_x, float *tensao) {
    float soma = 0.0f;
    int i;

    adc_select_input(2); // Seleciona o pino 28 como entrada ADC

    for(i=0;i<1000;i++) {
        soma += (float)adc_read();
        sleep_ms(1);
    }

    *tensao = ( ( ( soma/1000.f ) * ADC_VREF ) / (float)ADC_RESOLUTION );
    *r_x = ( ( *tensao * (float)R_CONHECIDO ) / (ADC_VREF - *tensao) );

    return 0;
}

// Função para encontrar o resistor E24 mais próximo
float resistor_e24(float resistencia_medida) {
    // Valores básicos da série E24
    const float e24_base[] = {
        10, 11, 12, 13, 15, 16, 18, 20, 22, 24, 27, 30,
        33, 36, 39, 43, 47, 51, 56, 62, 68, 75, 82, 91
    };
    const int n = sizeof(e24_base) / sizeof(e24_base[0]);

    float melhor_resistor = 0.0f;
    float menor_erro = 1e9;

    // Encontrar a década do valor medido
    float decada = powf(10.0f, floorf(log10f(resistencia_medida)));

    // Testar resistores da década atual e vizinhas (para bordas)
    for (int dec = -1; dec <= 1; dec++) {
        float decada_atual = decada * powf(10.0f, (float)dec);
        for (int i = 0; i < n; i++) {
            float candidato = e24_base[i] * decada_atual / 10.0f; // Dividimos por 10 porque e24_base começa em 10
            float erro = fabsf(candidato - resistencia_medida);
            if (erro < menor_erro) {
                menor_erro = erro;
                melhor_resistor = candidato;
            }
        }
    }

    return melhor_resistor;
}

void mostrar_resistor(float resistencia) {
    int sig1 = 0, sig2 = 0, multiplicador = 0;

    if (resistencia < 1) resistencia *= 1000; // transforma ohms fracionários em milivolts (ex: 0.22 Ohm vira 220 Ohm)

    // Normaliza para dois dígitos significativos
    while (resistencia >= 100) {
        resistencia /= 10;
        multiplicador++;
    }
    while (resistencia < 10) {
        resistencia *= 10;
        multiplicador--;
    }

    int valor = (int)(resistencia + 0.5); // Arredonda
    sig1 = valor / 10;
    sig2 = valor % 10;

    // Proteção
    if (sig1 > 9) sig1 = 9;
    if (sig2 > 9) sig2 = 9;
    if (multiplicador < -2) multiplicador = -2; // Para resistores muito pequenos
    if (multiplicador > 9) multiplicador = 9;    // Para resistores muito grandes

    // Atualiza LEDs 1, 2 e 3
    leds[3].R = resistor_colors[sig1].R;
    leds[3].G = resistor_colors[sig1].G;
    leds[3].B = resistor_colors[sig1].B;

    leds[2].R = resistor_colors[sig2].R;
    leds[2].G = resistor_colors[sig2].G;
    leds[2].B = resistor_colors[sig2].B;

    leds[1].R = resistor_colors[multiplicador].R;
    leds[1].G = resistor_colors[multiplicador].G;
    leds[1].B = resistor_colors[multiplicador].B;

    printf("Seg1: %d / Seg2: %d / Mult: %d\n",sig1,sig2,multiplicador);

    npWrite(); // Atualiza os LEDs
}

void obter_cores_resistor(float resistencia, char seg1[5], char seg2[5], char seg3[5]) {
    int sig1 = 0, sig2 = 0, multiplicador = 0;

    if (resistencia < 1) resistencia *= 1000; // Corrige valores pequenos

    // Normaliza para dois dígitos
    while (resistencia >= 100) {
        resistencia /= 10;
        multiplicador++;
    }
    while (resistencia < 10) {
        resistencia *= 10;
        multiplicador--;
    }

    int valor = (int)(resistencia + 0.5); // Arredonda
    sig1 = valor / 10;
    sig2 = valor % 10;

    // Proteções
    if (sig1 > 9) sig1 = 9;
    if (sig2 > 9) sig2 = 9;
    if (multiplicador < -2) multiplicador = -2;
    if (multiplicador > 9) multiplicador = 9;

    // Preenche as strings
    strncpy(seg1, nome_cores[sig1], 4);
    seg1[4] = '\0';

    strncpy(seg2, nome_cores[sig2], 4);
    seg2[4] = '\0';

    strncpy(seg3, nome_cores[multiplicador], 4);
    seg3[4] = '\0';
}

void draw_resistor(ssd1306_t *ssd) {
    ssd1306_rect(ssd, 25, 11, 106, 10, true, false);
    ssd1306_hline(ssd,3,10,30,true);
    ssd1306_hline(ssd,117,124,30,true);
    ssd1306_vline(ssd,24,26,33,true);
    ssd1306_vline(ssd,25,26,33,true);
    ssd1306_vline(ssd,63,26,33,true);
    ssd1306_vline(ssd,64,26,33,true);
    ssd1306_vline(ssd,102,26,33,true);
    ssd1306_vline(ssd,103,26,33,true);
}

int main() {
    float r_x;     // Armazena o valor da resistência desconhecida
    float tensao;  // Armazena o valor de tensão lido pelo ADC
    float r_e24;   // Armazena o valor do resistor da série E24 mais próxima
    char res[7];
    char volt[6];
    char seg1[5];
    char seg2[5];
    char seg3[5];
    ssd1306_t ssd; // Estrutura que representa o display OLED

    stdio_init_all(); // Inicializa as entradas e saídas padrões

    init_display(&ssd); // Inicializa o display OLED

    ssd1306_rect(&ssd, 0, 0, 128, 64, true, false); // Desenha a borda externa
    ssd1306_draw_string(&ssd,"res:",18,43);
    ssd1306_draw_string(&ssd,"volt:",77,43);
    ssd1306_vline(&ssd,63,41,62,true);
    ssd1306_vline(&ssd,64,41,62,true);
    ssd1306_hline(&ssd,1,126,40,true);
    ssd1306_hline(&ssd,1,126,39,true);
    draw_resistor(&ssd);
    ssd1306_send_data(&ssd); // Envia os dados para escrever no display

    npInit(MATRIX_PIN); // Inicializa e limpa a matriz de LEDs
    npClear();
    npWrite();

    adc_init(); // Inicializa o ADC
    adc_gpio_init(ADC_PIN); // Inicializa o pino 28 como entrada analógica

    init_button();  // Inicializa o botão B

    gpio_set_irq_enabled_with_callback(BUTTON_B, GPIO_IRQ_EDGE_FALL, true, &gpio_irq_callback); // Configura a interrupção para o botão B

    while (true) {
        ler_resistor(&r_x,&tensao);
        r_e24 = resistor_e24(r_x);
        mostrar_resistor(r_e24);
        printf("RESISTENCIA: %f/ TENSAO: %f/ Resistor: %f\n ",r_x,tensao,r_e24);

        snprintf(res, sizeof(res), "%06d", (int)(r_x+0.5f));
        snprintf(volt, sizeof(volt), "%05.3f", tensao);
        printf("%s\n",res);
        printf("%s\n",volt);

        ssd1306_draw_string(&ssd,res,8,53);
        ssd1306_draw_string(&ssd,volt,76,53);

        obter_cores_resistor(r_x, seg1, seg2, seg3);
        ssd1306_draw_string(&ssd,seg1,10,4);
        ssd1306_draw_string(&ssd,seg2,49,4);
        ssd1306_draw_string(&ssd,seg3,88,4);
        obter_cores_resistor(r_e24, seg1, seg2, seg3);
        ssd1306_draw_string(&ssd,seg1,10,13);
        ssd1306_draw_string(&ssd,seg2,49,13);
        ssd1306_draw_string(&ssd,seg3,88,13);
        
        ssd1306_send_data(&ssd); // Envia os dados para escrever no display
        
    }
}