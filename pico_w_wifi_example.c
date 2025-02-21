#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include <stdio.h>
#include "hardware/adc.h"
#include "pico/cyw43_arch.h"
#include "lwip/tcp.h"

// Definição dos pinos
#define LED_PIN 12        // Pino do LED (GP12)
#define BUZZER_A_PIN 21   // Pino do Buzzer A (GP21)
#define BUZZER_B_PIN 10   // Pino do Buzzer B (GP10)
#define BUTTON_ALARM_ON 5  // Botão para ligar alarmes (GP5)
#define BUTTON_ALARM_OFF 6 // Botão para desligar alarmes (GP6)
#define VSYS_PIN 29        // Pino para leitura da tensão VSYS (GP29)

// Definições para leitura da tensão VSYS
#define USB_CONNECTED_VOLTAGE 4.7f  // Consideramos USB conectado acima de 4.7V
#define NUM_AMOSTRAS 10             // Número de leituras para média móvel

// Configurações do Wi-Fi
#define WIFI_SSID "As 3 Marias"  // Nome da rede Wi-Fi
#define WIFI_PASS "DRC290479"    // Senha da rede Wi-Fi

// Estado do alarme
bool alarme_ativo = false;

// Buffer para respostas HTTP
#define HTTP_RESPONSE "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=UTF-8\r\n\r\n" \
"<!DOCTYPE html>" \
"<html>" \
"<head><title>Status do Alarme</title>" \
"<meta charset='UTF-8'>" \
"<style> body { background-color: black; color: white; } </style></head>" \
"<body>" \
"<center>" \
"<h1 style='font-size: 30px;'> AMBIENTE DE TESTE - NÃO LIGUE PARA APARÊNCIA </h1>" \
"<hr>" \
"<h1>Status do Alarme </h1>" \
"<h2>Alarme: <b>%s</b></h2>" \
"<hr>" \
"<h3>Controles</h3>" \
"<p><a href='/alarme/on'> <b>Ligar Alarme</b></a></p>" \
"<p><a href='/alarme/off'> <b>Desligar Alarme</b></a></p>" \
"</center>" \
"</body>" \
"</html>\r\n"

// Função de callback para processar requisições HTTP
static err_t http_callback(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
    if (p == NULL) {
        // Cliente fechou a conexão
        tcp_close(tpcb);
        return ERR_OK;
    }

    // Processa a requisição HTTP
    char *request = (char *)p->payload;

    if (strstr(request, "GET /alarme/on")) {
        alarme_ativo = true;  // Liga o alarme
    } else if (strstr(request, "GET /alarme/off")) {
        alarme_ativo = false; // Desliga o alarme
    }

    // Prepara a resposta HTTP
    char response[512];
    snprintf(response, sizeof(response), HTTP_RESPONSE, alarme_ativo ? "Ligado" : "Desligado");

    // Envia a resposta HTTP
    tcp_write(tpcb, response, strlen(response), TCP_WRITE_FLAG_COPY);

    // Libera o buffer recebido
    pbuf_free(p);

    return ERR_OK;
}

// Callback de conexão: associa o http_callback à conexão
static err_t connection_callback(void *arg, struct tcp_pcb *newpcb, err_t err) {
    tcp_recv(newpcb, http_callback);  // Associa o callback HTTP
    return ERR_OK;
}

// Função de setup do servidor TCP
static void start_http_server(void) {
    struct tcp_pcb *pcb = tcp_new();
    if (!pcb) {
        printf("Erro ao criar PCB\n");
        return;
    }

    // Liga o servidor na porta 80
    if (tcp_bind(pcb, IP_ADDR_ANY, 80) != ERR_OK) {
        printf("Erro ao ligar o servidor na porta 80\n");
        return;
    }

    pcb = tcp_listen(pcb);  // Coloca o PCB em modo de escuta
    tcp_accept(pcb, connection_callback);  // Associa o callback de conexão

    printf("Servidor HTTP rodando na porta 80...\n");
}

// Variáveis para controle do PWM
uint slice_num_a;  // Slice do PWM para o Buzzer A
uint channel_a;    // Canal do PWM para o Buzzer A
uint slice_num_b;  // Slice do PWM para o Buzzer B
uint channel_b;    // Canal do PWM para o Buzzer B

// Função para configurar o PWM nos buzzers
void configurar_pwm() {
    // Configura o pino do Buzzer A como saída PWM
    gpio_set_function(BUZZER_A_PIN, GPIO_FUNC_PWM);
    slice_num_a = pwm_gpio_to_slice_num(BUZZER_A_PIN);
    channel_a = pwm_gpio_to_channel(BUZZER_A_PIN);

    // Configura o pino do Buzzer B como saída PWM
    gpio_set_function(BUZZER_B_PIN, GPIO_FUNC_PWM);
    slice_num_b = pwm_gpio_to_slice_num(BUZZER_B_PIN);
    channel_b = pwm_gpio_to_channel(BUZZER_B_PIN);

    // Configura o PWM para ambos os buzzers
    pwm_set_wrap(slice_num_a, 62500);  // Define a frequência para 2 kHz (125000000 / 62500)
    pwm_set_chan_level(slice_num_a, channel_a, 31250);  // Duty cycle de 50% (31250 / 62500)
    pwm_set_wrap(slice_num_b, 62500);  // Define a frequência para 2 kHz (125000000 / 62500)
    pwm_set_chan_level(slice_num_b, channel_b, 31250);  // Duty cycle de 50% (31250 / 62500)
}

// Função para emitir o padrão de bipes
void emitir_bipes() {
    gpio_put(LED_PIN, 1);  // Liga o LED

    // Aciona os buzzers
    pwm_set_enabled(slice_num_a, true);
    pwm_set_enabled(slice_num_b, true);
    sleep_ms(500);  // Duração do beep

    gpio_put(LED_PIN, 0);  // Desliga o LED
    // Desliga os buzzers
    pwm_set_enabled(slice_num_a, false);
    pwm_set_enabled(slice_num_b, false);
    sleep_ms(200);  // Intervalo entre bipes
}

// Função para ativar o alarme com bipes contínuos
void iniciar_alerta() {
    alarme_ativo = true;
    printf("Queda de energia detectada!\n");

    // Loop infinito para manter o alarme ativado
    while (alarme_ativo) {
        emitir_bipes();  // Emitir o padrão de bipes

        // Verificar se o botão B foi pressionado para desligar o alarme
        if (gpio_get(BUTTON_ALARM_OFF) == 0) {
            parar_alerta();  // Chama a função para desligar o alarme
            break;  // Sai do loop
        }
    }
}

// Função para desativar o alarme
void parar_alerta() {
    gpio_put(LED_PIN, 0);  // Desliga o LED
    // Desabilita o PWM nos buzzers
    pwm_set_enabled(slice_num_a, false);
    pwm_set_enabled(slice_num_b, false);

    // Atualiza o estado do alarme
    alarme_ativo = false;

    printf("✅ Alarmes desativados!\n");
}

// Função para ler a tensão VSYS
float ler_tensao_vsys() {
    adc_select_input(2);  // Usa o canal 2
    uint16_t valor_adc = adc_read();
    float tensao = valor_adc * 3.3f / (1 << 12);
    return tensao * 3.0f;
}

int main() {
    stdio_init_all();
    adc_init();
    adc_gpio_init(VSYS_PIN);

    // Inicialização dos pinos
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);

    gpio_init(BUZZER_A_PIN);
    gpio_set_dir(BUZZER_A_PIN, GPIO_OUT);

    gpio_init(BUZZER_B_PIN);
    gpio_set_dir(BUZZER_B_PIN, GPIO_OUT);

    gpio_init(BUTTON_ALARM_ON);
    gpio_set_dir(BUTTON_ALARM_ON, GPIO_IN);
    gpio_pull_up(BUTTON_ALARM_ON);

    gpio_init(BUTTON_ALARM_OFF);
    gpio_set_dir(BUTTON_ALARM_OFF, GPIO_IN);
    gpio_pull_up(BUTTON_ALARM_OFF);

    // Configura o PWM nos buzzers
    configurar_pwm();

    // Inicializa o Wi-Fi
    if (cyw43_arch_init()) {
        printf("Erro ao inicializar o Wi-Fi\n");
        return 1;
    }

    cyw43_arch_enable_sta_mode();
    printf("Conectando ao Wi-Fi...\n");

    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASS, CYW43_AUTH_WPA2_AES_PSK, 10000)) {
        printf("Falha ao conectar ao Wi-Fi\n");
        return 1;
    } else {
        printf("Conectado ao Wi-Fi!\n");
        uint8_t *ip_address = (uint8_t*)&(cyw43_state.netif[0].ip_addr.addr);
        printf("Endereço IP %d.%d.%d.%d\n", ip_address[0], ip_address[1], ip_address[2], ip_address[3]);
    }

    // Inicia o servidor HTTP
    start_http_server();

    float soma_tensao = 0;
    float leituras[NUM_AMOSTRAS] = {0};
    int indice = 0;
    bool usb_conectado = true;
    int leituras_desconectado = 0;

    while (1) {
        // Verifica se o botão A foi pressionado para ativar o alarme
        if (gpio_get(BUTTON_ALARM_ON) == 0 || alarme_ativo) {
            iniciar_alerta();
            sleep_ms(300);  // Debounce básico
        }

        // Verifica a tensão VSYS para detectar desconexão do USB
        float tensao_vsys = ler_tensao_vsys();

        soma_tensao -= leituras[indice];
        leituras[indice] = tensao_vsys;
        soma_tensao += tensao_vsys;
        indice = (indice + 1) % NUM_AMOSTRAS;

        float media_tensao = soma_tensao / NUM_AMOSTRAS;

        printf("Média Tensão VSYS: %.2f V\n", media_tensao);

        if (media_tensao < USB_CONNECTED_VOLTAGE) {
            leituras_desconectado++;
        } else {
            leituras_desconectado = 0;
        }

        if (leituras_desconectado >= NUM_AMOSTRAS) {
            if (usb_conectado) {
                printf("USB desconectado! Emitindo bip de alerta...\n");
                iniciar_alerta();
            }
            usb_conectado = false;
        } else {
            if (!usb_conectado) {
                parar_alerta();
                printf("USB reconectado!\n");
            }
            usb_conectado = true;
        }

        cyw43_arch_poll();  // Necessário para manter o Wi-Fi ativo
        sleep_ms(100);
    }

    cyw43_arch_deinit();  // Desliga o Wi-Fi (não será chamado, pois o loop é infinito)
    return 0;
}