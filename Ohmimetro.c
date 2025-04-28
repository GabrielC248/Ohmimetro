// ---------------- Bibliotecas - Início ----------------

// Bibliotecas padrão do C (usadas para depuração e cálculos matemáticos)
#include <stdio.h> // Funções de entrada e saída padrão
#include <math.h>  // Funções matemáticas como powf, fabsf, etc.

// Bibliotecas do pico SDK de mais alto nível
#include "pico/stdlib.h"     // Funcionalidades básicas do RP2040
#include "pico/cyw43_arch.h"
#include "pico/bootrom.h"    // Para entrar no modo bootsel ao pressionar o botão B

// Bibliotecas do pico SDK de hardware
#include "hardware/i2c.h"    // Comunicação I2C
#include "hardware/pio.h"    // Controle da matriz de LEDs por PIO
#include "hardware/adc.h"    // Conversor analógico-digital
#include "hardware/clocks.h" // Controle dos clocks do sistema (PIO)

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
#define NUM_PIXELS 25 // Número total de LEDs na matriz
#define WS2812_PIN 7  // Pino da matriz de LEDs

// Configuração do botão
#define BUTTON_B 6 // Pino do botão B

// Configuração para o ohmímetro
#define ADC_PIN 28          // GPIO de leitura
#define ADC_VREF 3.30f      // Tensão de referência do ADC
#define ADC_RESOLUTION 4095 // Resolução do ADC (12 bits)
#define R_CONHECIDO 9920    // Resistor conhecido

// ---------------- Definições - Fim ----------------



// ---------------- Variáveis - Início ----------------

static volatile uint32_t last_time = 0; // Armazena o último tempo registrado nas interrupções

// Variáveis da matriz de LEDs
static volatile uint32_t leds[NUM_PIXELS]; // Buffer de cores para cada LED
static PIO pio;     // Instância do PIO
static uint sm;     // State machine usada no PIO
static uint offset; // Offset do programa no PIO

// Struct para facilitar a escrita na matriz de LEDs
typedef struct {
    uint8_t R, G, B;
}Color;

// Tabela de cores associadas aos dígitos do código de cores de resistores
static const Color resistor_colors[10] = {
    {0, 0, 0},  // 0 - Preto
    {8, 1, 0},  // 1 - Marrom
    {8, 0, 0},  // 2 - Vermelho
    {15, 3, 0}, // 3 - Laranja
    {10, 4, 0}, // 4 - Amarelo
    {0, 8, 0},  // 5 - Verde
    {0, 0, 8},  // 6 - Azul
    {6, 0, 6},  // 7 - Violeta 
    {1, 1, 1},  // 8 - Cinza
    {8, 8, 8}   // 9 - Branco
};

// Tabela de nomes curtos das cores (para exibição no display)
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

// Envia a cor de um pixel para o PIO
static inline void put_pixel(PIO pio, uint sm, uint32_t pixel_grb) {
    pio_sm_put_blocking(pio, sm, pixel_grb << 8u);
}

// Codifica cores RGB em formato 24 bits
 static inline uint32_t urgb_u32(uint8_t r, uint8_t g, uint8_t b) {
    return
            ((uint32_t) (r) << 8) |
            ((uint32_t) (g) << 16) |
            (uint32_t) (b);
}

// Define a cor de um LED da matriz
void matrix_set_led(int index, uint8_t r, uint8_t g, uint8_t b) {
    if (index >= 0 && index < NUM_PIXELS) {
        leds[index] = urgb_u32(r, g, b);
    }
}

// Apaga todos os LEDs da matriz
void matrix_clear_leds() {
    for (int i = 0; i < NUM_PIXELS; i++) {
        leds[i] = urgb_u32(0, 0, 0);
    }
}

// Atualiza os LEDs físicos com as cores do buffer
void matrix_write(PIO pio, uint sm) {
    for (int i = 0; i < NUM_PIXELS; i++) {
        put_pixel(pio, sm, leds[i]);
    }
}

// Inicializa a matriz de LEDs
void matrix_init() {
    bool success = pio_claim_free_sm_and_add_program_for_gpio_range(&ws2812_program, &pio, &sm, &offset, WS2812_PIN, 1, true);
    hard_assert(success);

    ws2812_program_init(pio, sm, offset, WS2812_PIN, 800000, false);
    matrix_clear_leds();
    matrix_write(pio,sm);
}

// -------- Matriz - Fim --------

// ---------------- Inicializações - Fim ----------------



// ---------------- Callback - Início ----------------

// Callback para tratar o botão B (reset para modo BOOTSEL)
void gpio_irq_callback(uint gpio, uint32_t events) {
    uint32_t current_time = to_ms_since_boot(get_absolute_time()); // Obtém o tempo atual em ms

    // Debounce de 200 ms
    if( (current_time - last_time) > 200 ) {
        if(gpio == BUTTON_B) {
            reset_usb_boot(0, 0);
        }
    }
}

// ---------------- Callback - Fim ----------------



// ---------------- Funções do ohmímetro - Início ----------------

// Lê a resistência desconhecida via ADC
int ler_resistor(float *r_x, float *tensao) {
    float soma = 0.0f;
    int i;

    adc_select_input(2); // Seleciona o pino 28 como entrada ADC

    // Faz 1000 leituras e calcula a média
    for(i=0;i<1000;i++) {
        soma += (float)adc_read();
        sleep_ms(1);
    }

    *tensao = ( ( ( soma/1000.f ) * ADC_VREF ) / (float)ADC_RESOLUTION );
    *r_x = ( ( *tensao * (float)R_CONHECIDO ) / (ADC_VREF - *tensao) );

    return 0;
}

// Encontra o resistor da série E24 mais próximo do valor lido pelo ADC
float resistor_e24(float resistencia_medida) {
    // Valores básicos da série E24
    const float e24_base[] = {
        10, 11, 12, 13, 15, 16, 18, 20, 22, 24, 27, 30,
        33, 36, 39, 43, 47, 51, 56, 62, 68, 75, 82, 91
    };
    const int n = sizeof(e24_base) / sizeof(e24_base[0]);

    float melhor_resistor = 0.0f;
    float menor_erro = 1e9;

    float decada_atual, candidato, erro;
    int dec, i;

    // Encontrar a década do valor medido
    float decada = powf(10.0f, floorf(log10f(resistencia_medida)));

    // Verifica décadas próximas
    for (dec = -1; dec <= 1; dec++) {
        decada_atual = decada * powf(10.0f, (float)dec);
        for (i = 0; i < n; i++) {
            candidato = e24_base[i] * decada_atual / 10.0f;
            erro = fabsf(candidato - resistencia_medida);
            if (erro < menor_erro) {
                menor_erro = erro;
                melhor_resistor = candidato;
            }
        }
    }

    return melhor_resistor;
}

// Atualiza os LEDs com as cores correspondentes da resistência e mostra as cores do resistor na matriz
void mostrar_resistor_matriz(float resistencia) {
    int sig1 = 0, sig2 = 0, multiplicador = 0, valor;

    if (resistencia < 1) resistencia *= 1000; // Corrige valores pequenos

    // Normaliza para dois dígitos significativos
    while (resistencia >= 100) {
        resistencia /= 10;
        multiplicador++;
    }
    while (resistencia < 10) {
        resistencia *= 10;
        multiplicador--;
    }

    valor = (int)(resistencia + 0.5); // Arredonda
    sig1 = valor / 10;
    sig2 = valor % 10;

    // Proteção
    if (sig1 > 9) sig1 = 9;
    if (sig2 > 9) sig2 = 9;
    if (multiplicador < -2) multiplicador = -2; // Para resistores muito pequenos
    if (multiplicador > 9) multiplicador = 9;   // Para resistores muito grandes

    // Debug
    printf("Seg1: %d / Seg2: %d / Mult: %d\n",sig1,sig2,multiplicador);

    // Atualiza LEDs 13, 12 e 11
    matrix_set_led(13,resistor_colors[sig1].R,resistor_colors[sig1].G,resistor_colors[sig1].B);
    matrix_set_led(12,resistor_colors[sig2].R,resistor_colors[sig2].G,resistor_colors[sig2].B);
    matrix_set_led(11,resistor_colors[multiplicador].R,resistor_colors[multiplicador].G,resistor_colors[multiplicador].B);
    
    // Atualiza os LEDs
    matrix_write(pio,sm);
}

// Obtém as cores correspondentes aos dígitos do resistor para exibição no OLED
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

// Desenha representação gráfica do resistor no OLED e na matriz de LEDs
void draw_resistors(ssd1306_t *ssd) {
    ssd1306_rect(ssd, 25, 11, 106, 10, true, false);
    ssd1306_hline(ssd,3,10,30,true);
    ssd1306_hline(ssd,117,124,30,true);
    ssd1306_vline(ssd,24,26,33,true);
    ssd1306_vline(ssd,25,26,33,true);
    ssd1306_vline(ssd,63,26,33,true);
    ssd1306_vline(ssd,64,26,33,true);
    ssd1306_vline(ssd,102,26,33,true);
    ssd1306_vline(ssd,103,26,33,true);
    matrix_set_led(6,1,1,1);
    matrix_set_led(7,1,1,1);
    matrix_set_led(8,1,1,1);
    matrix_set_led(10,1,1,1);
    matrix_set_led(14,1,1,1);
    matrix_set_led(16,1,1,1);
    matrix_set_led(17,1,1,1);
    matrix_set_led(18,1,1,1);
}

// ---------------- Funções do ohmímetro - Fim ----------------



int main() {
    float r_x;     // Armazena o valor da resistência desconhecida lida
    float tensao;  // Armazena o valor da tensão lida pelo ADC
    float r_e24;   // Armazena o valor do resistor da série E24 mais próximo encontrado
    char res[7];   // Buffer para armazenar a resistência formatada como string
    char volt[6];  // Buffer para armazenar a tensão formatada como string
    char seg1[5];  // Buffer para a primeira faixa de cor do resistor
    char seg2[5];  // Buffer para a segunda faixa de cor do resistor
    char seg3[5];  // Buffer para a terceira faixa de cor do resistor
    ssd1306_t ssd; // Estrutura que representa o display OLED

    stdio_init_all(); // Inicializa as entradas e saídas padrões

    matrix_init(); // Inicializa a matriz de LEDs

    init_display(&ssd); // Inicializa o display OLED

    // Desenha a borda do display
    ssd1306_rect(&ssd, 0, 0, 128, 64, true, false);

    // Desenha os rótulos "res:" e "volt:" no display
    ssd1306_draw_string(&ssd,"res:",18,43);
    ssd1306_draw_string(&ssd,"volt:",77,43);

    // Desenha linhas verticais e horizontais para separar as áreas do display
    ssd1306_vline(&ssd,63,41,62,true);
    ssd1306_vline(&ssd,64,41,62,true);
    ssd1306_hline(&ssd,1,126,40,true);
    ssd1306_hline(&ssd,1,126,39,true);

    // Desenha a representação do resistor no display e na matriz de LEDs
    draw_resistors(&ssd);

    ssd1306_send_data(&ssd); // Envia os dados para escrever no display
    matrix_write(pio, sm);   // Envia os dados para escrever na matriz

    adc_init();             // Inicializa o ADC
    adc_gpio_init(ADC_PIN); // Inicializa o pino 28 como entrada analógica

    init_button(); // Inicializa o botão B

    gpio_set_irq_enabled_with_callback(BUTTON_B, GPIO_IRQ_EDGE_FALL, true, &gpio_irq_callback); // Configura a interrupção para o botão B

    while (true) {

        ler_resistor(&r_x,&tensao); // Calcula a tensão no divisor e o valor do resistor desconhecido

        r_e24 = resistor_e24(r_x); // Calcula o resistor mais próximo da série E24

        printf("r_x: %f/ tensao: %f/ r_e24: %f\n ",r_x,tensao,r_e24); // Debug no terminal

        mostrar_resistor_matriz(r_e24); // Mostra as cores do resistor E24 na matriz de LEDs

        snprintf(res, sizeof(res), "%06d", (int)(r_x+0.5f)); // Formata a resistência como string e coloca no buffer
        snprintf(volt, sizeof(volt), "%05.3f", tensao);      // Formata a tensão como string e coloca no buffer
        
        // Debug dos valores formatados
        printf("%s\n",res);
        printf("%s\n",volt);

        ssd1306_draw_string(&ssd,res,8,53);   // Escreve no display OLED o valor da resistência calculada
        ssd1306_draw_string(&ssd,volt,76,53); // Escreve no display OLED o valor da tensão calculada

        // Obtém as cores das faixas do resistor lido (r_x) e escreve no display
        obter_cores_resistor(r_x, seg1, seg2, seg3);
        ssd1306_draw_string(&ssd,seg1,10,4);
        ssd1306_draw_string(&ssd,seg2,49,4);
        ssd1306_draw_string(&ssd,seg3,88,4);

        // Obtém as cores das faixas para o resistor E24 mais próximo e escreve no display
        obter_cores_resistor(r_e24, seg1, seg2, seg3);
        ssd1306_draw_string(&ssd,seg1,10,13);
        ssd1306_draw_string(&ssd,seg2,49,13);
        ssd1306_draw_string(&ssd,seg3,88,13);
        
        ssd1306_send_data(&ssd); // Envia os dados para escrever no display
    }
}