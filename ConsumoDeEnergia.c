#include <stdio.h>
#include <stdlib.h> // Para malloc, free, exit, strtod
#include <string.h> // Para strtok, strcpy
#include <math.h>   // Para sqrt, fabs, isnan
#include <locale.h> // Para setlocale (ler vírgulas corretamente)

// Constantes
#define MAX_DIAS 400     // Tamanho máximo do vetor
#define MAX_LINHA 1024   // Tamanho máximo de uma linha do CSV
#define JANELA_OUTLIER 2 // Janela de ±2 dias para mediana do outlier
#define Z_SCORE_LIMITE 3.0 // Limite Z-score para outliers

// Estrutura para armazenar os dados de um dia
typedef struct {
    int dia;
    char data[11]; // "YYYY-MM-DD"
    double temp;
    double umidade;
    double irradiancia;
    double vento;
    double ocupacao;
    int diaUtil;
    int feriado;
    double tarifaPonta;
    double consumo;
    double geracaoFV;
    double cargaVE;
    double importacaoRede;

    // Campos adicionais para análise
    double consumoLiquido;
    double zscoreConsumo;
    int ehOutlier;

} RegistroEnergia;

// --- Protótipos ---
int lerCSV(const char* nomeArquivo, RegistroEnergia dados[], int maxRegistros);
void tratarDados(RegistroEnergia dados[], int n);
void analisarDados(RegistroEnergia dados[], int n);
void preverConsumo(RegistroEnergia dados[], int n);

// --- Função de Leitura ---
int lerCSV(const char* nomeArquivo, RegistroEnergia dados[], int maxRegistros) {
    FILE* fp = fopen(nomeArquivo, "r");
    if (fp == NULL) {
        return -1;
    }

    char linha[MAX_LINHA];
    int n = 0;

    // Pular cabeçalho
    if (fgets(linha, MAX_LINHA, fp) == NULL) {
        fclose(fp);
        return 0;
    }

    // Ler dados (usando ; como separador)
    while (n < maxRegistros && fgets(linha, MAX_LINHA, fp) != NULL) {
        // Formato: Dia;Data;Temp;...
        int camposLidos = sscanf(linha, "%d;%*[^;];%lf;%lf;%lf;%lf;%lf;%d;%d;%lf;%lf;%lf;%lf;%lf",
               &dados[n].dia, &dados[n].temp, &dados[n].umidade,
               &dados[n].irradiancia, &dados[n].vento, &dados[n].ocupacao,
               &dados[n].diaUtil, &dados[n].feriado, &dados[n].tarifaPonta,
               &dados[n].consumo, &dados[n].geracaoFV, &dados[n].cargaVE,
               &dados[n].importacaoRede);
        
        if (camposLidos == 13) {
            n++;
        }
    }

    fclose(fp);
    return n;
}

// --- Funções Auxiliares de Tratamento ---
double medianaJanela(RegistroEnergia dados[], int indice, int n) {
    double soma = 0;
    int count = 0;
    for (int i = indice - JANELA_OUTLIER; i <= indice + JANELA_OUTLIER; i++) {
        if (i >= 0 && i < n && !dados[i].ehOutlier) {
            soma += dados[i].consumo;
            count++;
        }
    }
    return (count > 0) ? (soma / count) : dados[indice].consumo;
}

// --- Função de Tratamento ---
void tratarDados(RegistroEnergia dados[], int n) {
    // 1. Tratar Negativos
    for (int i = 0; i < n; i++) {
        if (dados[i].consumo < 0) dados[i].consumo = (i > 0) ? dados[i-1].consumo : 0;
        if (dados[i].geracaoFV < 0) dados[i].geracaoFV = (i > 0) ? dados[i-1].geracaoFV : 0;
    }

    // 2. Tratar Outliers (Z-Score > 3)
    if (n == 0) return;

    double somaConsumo = 0;
    for (int i = 0; i < n; i++) somaConsumo += dados[i].consumo;
    double mediaConsumo = somaConsumo / n;

    double somaQuadConsumo = 0;
    for (int i = 0; i < n; i++) somaQuadConsumo += pow(dados[i].consumo - mediaConsumo, 2);
    double stdDevConsumo = sqrt(somaQuadConsumo / n);

    printf("\n--- Tratamento de Outliers ---\n");
    printf("Media: %.2f, Desvio Padrao: %.2f\n", mediaConsumo, stdDevConsumo);

    if (stdDevConsumo > 0) {
        for (int i = 0; i < n; i++) {
            dados[i].zscoreConsumo = (dados[i].consumo - mediaConsumo) / stdDevConsumo;
            dados[i].ehOutlier = (fabs(dados[i].zscoreConsumo) > Z_SCORE_LIMITE);
            
            if (dados[i].ehOutlier) {
                printf("Outlier Dia %d: %.2f (Z=%.2f). Corrigindo...\n", dados[i].dia, dados[i].consumo, dados[i].zscoreConsumo);
                dados[i].consumo = medianaJanela(dados, i, n);
            }
        }
    }
}

// --- Função Auxiliar de Correlação ---
double calcularCorrelacao(RegistroEnergia dados[], int n, const char* var1, const char* var2) {
    double somaX = 0, somaY = 0, somaXY = 0, somaX2 = 0, somaY2 = 0;
    
    for (int i = 0; i < n; i++) {
        double x, y;
        if (strcmp(var1, "consumo") == 0) x = dados[i].consumo;
        else if (strcmp(var1, "temp") == 0) x = dados[i].temp;
        // ... adicionar outros se necessário para x
        
        if (strcmp(var2, "consumo") == 0) y = dados[i].consumo;
        else if (strcmp(var2, "temp") == 0) y = dados[i].temp;
        else if (strcmp(var2, "umidade") == 0) y = dados[i].umidade;
        else if (strcmp(var2, "ocupacao") == 0) y = dados[i].ocupacao;
        else if (strcmp(var2, "irradiancia") == 0) y = dados[i].irradiancia;
        else if (strcmp(var2, "diaUtil") == 0) y = (double)dados[i].diaUtil;

        somaX += x; somaY += y; somaXY += x * y; somaX2 += x * x; somaY2 += y * y;
    }
    double numerador = (n * somaXY) - (somaX * somaY);
    double denominador = sqrt(((n * somaX2) - pow(somaX, 2)) * ((n * somaY2) - pow(somaY, 2)));
    return (denominador == 0) ? 0 : numerador / denominador;
}

// --- Função de Análise (CORRIGIDA) ---
void analisarDados(RegistroEnergia dados[], int n) {
    printf("\n--- Analise Estatistica ---\n");

    if (n == 0) return;

    // Calcular Consumo Líquido
    for (int i = 0; i < n; i++) {
        dados[i].consumoLiquido = dados[i].consumo - dados[i].geracaoFV;
    }

    // Inicialização
    double minCons = dados[0].consumo, maxCons = dados[0].consumo, somaCons = 0;
    double minFV = dados[0].geracaoFV, maxFV = dados[0].geracaoFV, somaFV = 0;
    double minImp = dados[0].importacaoRede, maxImp = dados[0].importacaoRede, somaImp = 0;

    for (int i = 0; i < n; i++) {
        // Somas
        somaCons += dados[i].consumo;
        somaFV += dados[i].geracaoFV;
        somaImp += dados[i].importacaoRede;

        // Min/Max Consumo
        if (dados[i].consumo < minCons) minCons = dados[i].consumo;
        if (dados[i].consumo > maxCons) maxCons = dados[i].consumo;

        // Min/Max Geração FV (CORREÇÃO APLICADA AQUI)
        if (dados[i].geracaoFV < minFV) minFV = dados[i].geracaoFV;
        if (dados[i].geracaoFV > maxFV) maxFV = dados[i].geracaoFV;

        // Min/Max Importação (CORREÇÃO APLICADA AQUI)
        if (dados[i].importacaoRede < minImp) minImp = dados[i].importacaoRede;
        if (dados[i].importacaoRede > maxImp) maxImp = dados[i].importacaoRede;
    }

    printf("Estatisticas Descritivas (N=%d dias):\n", n);
    printf("  Consumo (kWh):    Media=%.2f  Min=%.2f  Max=%.2f\n", somaCons/n, minCons, maxCons);
    printf("  Geracao FV (kWh): Media=%.2f  Min=%.2f  Max=%.2f\n", somaFV/n, minFV, maxFV);
    printf("  Importacao (kWh): Media=%.2f  Min=%.2f  Max=%.2f\n", somaImp/n, minImp, maxImp);

    // Correlações
    printf("\nCorrelacoes (vs Consumo):\n");
    printf("  vs Temperatura: %.4f\n", calcularCorrelacao(dados, n, "consumo", "temp"));
    printf("  vs Umidade:     %.4f\n", calcularCorrelacao(dados, n, "consumo", "umidade"));
    printf("  vs Ocupacao:    %.4f\n", calcularCorrelacao(dados, n, "consumo", "ocupacao"));
    printf("  vs Irradiancia: %.4f\n", calcularCorrelacao(dados, n, "consumo", "irradiancia"));
    
    // Comparação Dia Útil
    double somaUtil = 0, somaFDS = 0;
    int nUtil = 0, nFDS = 0;
    for (int i=0; i<n; i++) {
        if (dados[i].diaUtil == 1 && dados[i].feriado == 0) {
            somaUtil += dados[i].consumo; nUtil++;
        } else {
            somaFDS += dados[i].consumo; nFDS++;
        }
    }
    printf("\nMedia Consumo: Dia Util (%.2f) vs FDS/Feriado (%.2f)\n", 
           (nUtil>0 ? somaUtil/nUtil : 0), (nFDS>0 ? somaFDS/nFDS : 0));
}

// --- Função de Previsão ---
void preverConsumo(RegistroEnergia dados[], int n) {
    if (n < 3) {
        printf("Dados insuficientes para previsao.\n");
        return;
    }
    
    printf("\n--- Previsao (Dia %d) ---\n", n + 1);

    // 1. Média Móvel 3
    double mm3 = (dados[n-1].consumo + dados[n-2].consumo + dados[n-3].consumo) / 3.0;
    printf("Previsao MM3: %.2f kWh\n", mm3);

    // 2. Regressão Linear Simples (Bônus)
    double somaX = 0, somaY = 0, somaXY = 0, somaX2 = 0;
    for (int i = 0; i < n; i++) {
        double x = dados[i].irradiancia;
        double y = dados[i].consumo;
        somaX += x; somaY += y; somaXY += x * y; somaX2 += x * x;
    }
    
    double mediaX = somaX / n;
    double mediaY = somaY / n;
    double numerador = somaXY - (n * mediaX * mediaY);
    double denominador = somaX2 - (n * mediaX * mediaX);
    
    if (denominador != 0) {
        double b1 = numerador / denominador;
        double b0 = mediaY - (b1 * mediaX);
        printf("Regressao Linear (Consumo ~ Irradiancia): y = %.2f + %.2f*x\n", b0, b1);
    }
}

// --- MAIN ---
int main() {
    // Configura localidade para usar vírgula em números e acentos
    setlocale(LC_ALL, ""); 
    
    static RegistroEnergia dados[MAX_DIAS];
    const char* arquivoEntrada = "consumo.csv";

    printf("Lendo arquivo '%s'...\n", arquivoEntrada);
    
    int n = lerCSV(arquivoEntrada, dados, MAX_DIAS);
    if (n <= 0) {
        printf("Erro: Nao foi possivel ler dados ou arquivo vazio.\n");
        return 1;
    }
    printf("Sucesso: %d dias lidos.\n", n);

    tratarDados(dados, n);
    analisarDados(dados, n);
    preverConsumo(dados, n);

    return 0;
}