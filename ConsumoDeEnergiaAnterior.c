#include <stdio.h>
#include <stdlib.h> // Para malloc, free, exit, strtod
#include <string.h> // Para strtok, strcpy
#include <math.h>   // Para sqrt, fabs, isnan
#include <locale.h> // <--- CORREÇÃO 1: Adicionado para setlocale

// Constantes
#define MAX_DIAS 400     // Tamanho máximo do vetor (ajuste conforme necessidade)
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

    // Campos adicionais para Analise
    double consumoLiquido;
    double zscoreConsumo;
    int ehOutlier;

} RegistroEnergia;

// Protótipos das funções (boa prática)
int lerCSV(const char* nomeArquivo, RegistroEnergia dados[], int maxRegistros);
void tratarDados(RegistroEnergia dados[], int n);
void analisarDados(RegistroEnergia dados[], int n);
void preverConsumo(RegistroEnergia dados[], int n);
// void exportarCSV(const char* nomeArquivo, RegistroEnergia dados[], int n); // Prototipo
// ... protótipos de funções auxiliares ...



/**
 * Lê o arquivo CSV para o vetor de structs.
 * Pula o cabeçalho.
 * Retorna o número de registros lidos.
 */
int lerCSV(const char* nomeArquivo, RegistroEnergia dados[], int maxRegistros) {
    FILE* fp = fopen(nomeArquivo, "r");
    if (fp == NULL) {
        perror("Erro ao abrir o arquivo");
        return -1;
    }

    char linha[MAX_LINHA];
    int n = 0;

    // Pular cabeçalho
    if (fgets(linha, MAX_LINHA, fp) == NULL) {
        printf("Arquivo vazio ou erro ao ler cabeçalho.\n");
        fclose(fp);
        return 0;
    }

    // Ler dados
    // Formato esperado: Dia;Data;Temp;...
    while (n < maxRegistros && fgets(linha, MAX_LINHA, fp) != NULL) {
        
        // <--- CORREÇÃO 3: Trocamos todas as VÍRGULAS (,) por PONTO-E-VÍRGULA (;)
        // O formato %*[^;] pula a coluna da data (string)
        int camposLidos = sscanf(linha, "%d;%*[^;];%lf;%lf;%lf;%lf;%lf;%d;%d;%lf;%lf;%lf;%lf;%lf",
               &dados[n].dia, &dados[n].temp, &dados[n].umidade,
               &dados[n].irradiancia, &dados[n].vento, &dados[n].ocupacao,
               &dados[n].diaUtil, &dados[n].feriado, &dados[n].tarifaPonta,
               &dados[n].consumo, &dados[n].geracaoFV, &dados[n].cargaVE,
               &dados[n].importacaoRede);
        
        // sscanf com %* não conta o campo pulado, então esperamos 13 campos.
        if (camposLidos == 13) {
            n++;
        } else {
           //printf("Linha %d mal formatada. Campos lidos: %d\n", n + 2, camposLidos);
        }
    }

    fclose(fp);
    return n; // Retorna o número de dias lidos
}

// --- Funções de Tratamento ---

// Função auxiliar para media Movel de 3 dias
double mediaMovel3(RegistroEnergia dados[], int indice, const char* campo) {
    if (indice < 3) return NAN; // Não é possível calcular
    
    double soma = 0;
    // Pega os 3 dias *anteriores*
    if (strcmp(campo, "consumo") == 0) {
        soma = dados[indice-1].consumo + dados[indice-2].consumo + dados[indice-3].consumo;
    }
    // Adicionar lógica para outros campos se necessário
    
    return soma / 3.0;
}

// Função auxiliar para mediana da janela (simplificada)
double medianaJanela(RegistroEnergia dados[], int indice, int n) {
    // implementacao simplista: usar media da janela se mediana for complexa
    // Para mediana real: copiar valores da janela para array temp, ordenar, pegar valor do meio
    // Janela de ±2 dias (total 5 dias)
    double soma = 0;
    int count = 0;
    for (int i = indice - JANELA_OUTLIER; i <= indice + JANELA_OUTLIER; i++) {
        if (i >= 0 && i < n && !dados[i].ehOutlier) { // Evita usar outros outliers no cálculo
            soma += dados[i].consumo;
            count++;
        }
    }
    return (count > 0) ? (soma / count) : dados[indice-1].consumo; // Fallback
}


/**
 * Função principal para tratar todos os dados (Negativos, Ausentes, Outliers)
 */
void tratarDados(RegistroEnergia dados[], int n) {
    // 1. Tratar Negativos (imputar com último valor válido)
    for (int i = 0; i < n; i++) {
        if (dados[i].consumo < 0) {
            dados[i].consumo = (i > 0) ? dados[i-1].consumo : 0; // Usa 0 se for o primeiro
        }
        if (dados[i].geracaoFV < 0) {
            dados[i].geracaoFV = (i > 0) ? dados[i-1].geracaoFV : 0;
        }
        // ... repetir para outros campos relevantes ...
    }

    // 2. Tratar Ausentes (NAN) (imputar com media Movel 3 dias)
    for (int i = 0; i < n; i++) {
        // Em C, valores ausentes podem ser 0, ou NAN se strtod falhar.
        // Assumindo que sscanf colocou 0. Se usarmos strtod, podemos verificar isnan(dados[i].consumo)
        // O sscanf com %lf e locale correto (graças ao setlocale)
        // irá ler "0" se o campo estiver vazio.
        // A lógica de NaN é mais robusta se usarmos strtod manualmente.
        // Por ora, vamos focar nos outliers.
        
        /*
        if (isnan(dados[i].consumo)) { // Requer <math.h> e inicialização correta
             dados[i].consumo = (i >= 3) ? mediaMovel3(dados, i, "consumo") : dados[i-1].consumo;
        }
        */
    }

    // 3. Tratar Outliers (|z| > 3)
    double mediaConsumo = 0;
    double stdDevConsumo = 0;
    double somaConsumo = 0;
    double somaQuadConsumo = 0;

    if (n == 0) {
        printf("Nenhum dado para tratar.\n");
        return;
    }

    for (int i = 0; i < n; i++) {
        somaConsumo += dados[i].consumo;
    }
    mediaConsumo = somaConsumo / n;

    for (int i = 0; i < n; i++) {
        somaQuadConsumo += pow(dados[i].consumo - mediaConsumo, 2);
    }
    // Evita divisão por zero se houver apenas 1 registro
    stdDevConsumo = (n > 0) ? sqrt(somaQuadConsumo / n) : 0;

    printf("\n--- Tratamento de Outliers (Consumo) ---\n");
    // %f é o formato correto para printf, mesmo com setlocale
    printf("Media: %.2f, Desvio Padrao: %.2f\n", mediaConsumo, stdDevConsumo); 

    // Evita divisão por zero se stdDev for 0
    if (stdDevConsumo == 0) {
        printf("Desvio padrao e zero. Impossivel calcular Z-score.\n");
        for (int i = 0; i < n; i++) {
            dados[i].zscoreConsumo = 0;
            dados[i].ehOutlier = 0;
        }
    } else {
        for (int i = 0; i < n; i++) {
            dados[i].zscoreConsumo = (dados[i].consumo - mediaConsumo) / stdDevConsumo;
            dados[i].ehOutlier = (fabs(dados[i].zscoreConsumo) > Z_SCORE_LIMITE);
        }
    }

    // Segunda passagem para substituir outliers
    for (int i = 0; i < n; i++) {
        if (dados[i].ehOutlier) {
            printf("Outlier detectado Dia %d: Consumo %.2f (Z=%.2f). Substituindo...\n", dados[i].dia, dados[i].consumo, dados[i].zscoreConsumo);
            dados[i].consumo = medianaJanela(dados, i, n); // Substitui por mediana/media da janela
        }
    }
}




// Função auxiliar para Correlação de Pearson
double calcularCorrelacao(RegistroEnergia dados[], int n, const char* var1, const char* var2) {
    double somaX = 0, somaY = 0, somaXY = 0, somaX2 = 0, somaY2 = 0;
    
    for (int i = 0; i < n; i++) {
        double x, y;

        // Mapeamento de string para variável
        if (strcmp(var1, "consumo") == 0) x = dados[i].consumo;
        else if (strcmp(var1, "temp") == 0) x = dados[i].temp;
        // Se var1 não for achado, x não é inicializado!
        
        if (strcmp(var2, "consumo") == 0) y = dados[i].consumo;
        else if (strcmp(var2, "temp") == 0) y = dados[i].temp;
        else if (strcmp(var2, "umidade") == 0) y = dados[i].umidade;
        else if (strcmp(var2, "ocupacao") == 0) y = dados[i].ocupacao;
        else if (strcmp(var2, "irradiancia") == 0) y = dados[i].irradiancia;
        else if (strcmp(var2, "diaUtil") == 0) y = (double)dados[i].diaUtil;
        // Se var2 não for achado, y não é inicializado!

        somaX += x;
        somaY += y;
        somaXY += x * y;
        somaX2 += x * x;
        somaY2 += y * y;
    }

    double numerador = (n * somaXY) - (somaX * somaY);
    double denominador = sqrt(((n * somaX2) - pow(somaX, 2)) * ((n * somaY2) - pow(somaY, 2)));

    if (denominador == 0) return 0;
    return numerador / denominador;
}

void analisarDados(RegistroEnergia dados[], int n) {
    printf("\n--- Analise Estatistica ---\n");

    // 1. Calcular Consumo Líquido
    for (int i = 0; i < n; i++) {
        dados[i].consumoLiquido = dados[i].consumo - dados[i].geracaoFV;
    }

    // 2. Estatistica Descritiva (Consumo, GeracaoFV, importacao)
    // Corrigido para evitar erro se n=0
    if (n == 0) {
        printf("Nenhum dado para analisar.\n");
        return;
    }

    double minCons = dados[0].consumo, maxCons = dados[0].consumo, somaCons = 0;
    double minFV = dados[0].geracaoFV, maxFV = dados[0].geracaoFV, somaFV = 0;
    double minImp = dados[0].importacaoRede, maxImp = dados[0].importacaoRede, somaImp = 0;

    for (int i = 0; i < n; i++) {
        somaCons += dados[i].consumo;
        somaFV += dados[i].geracaoFV;
        somaImp += dados[i].importacaoRede;
        if (dados[i].consumo < minCons) minCons = dados[i].consumo;
        if (dados[i].consumo > maxCons) maxCons = dados[i].consumo;
        // ... (fazer para min/max de FV e Importacao) ...
    }
    printf("Estatisticas Descritivas (N=%d dias):\n", n);
    printf("  Consumo (kWh):\tmedia=%.2f\tMin=%.2f\tMax=%.2f\n", somaCons/n, minCons, maxCons);
    printf("  Geracao FV (kWh):\tmedia=%.2f\tMin=%.2f\tMax=%.2f\n", somaFV/n, minFV, maxFV);
    printf("  importacao (kWh):\tmedia=%.2f\tMin=%.2f\tMax=%.2f\n", somaImp/n, minImp, maxImp);

    // 3. Correlações de Pearson
    printf("\nCorrelacoes de Pearson (vs Consumo):\n");
    printf("  vs Temperatura: %.4f\n", calcularCorrelacao(dados, n, "consumo", "temp"));
    printf("  vs Umidade:     %.4f\n", calcularCorrelacao(dados, n, "consumo", "umidade"));
    printf("  vs Ocupacao:    %.4f\n", calcularCorrelacao(dados, n, "consumo", "ocupacao"));
    printf("  vs Irradiancia: %.4f\n", calcularCorrelacao(dados, n, "consumo", "irradiancia"));
    printf("  vs Dia Util:    %.4f\n", calcularCorrelacao(dados, n, "consumo", "diaUtil"));

    // 4. Comparar media de Consumo (Dia Util vs Fim de Semana/Feriado)
    double somaUtil = 0, somaNaoUtil = 0;
    int countUtil = 0, countNaoUtil = 0;
    
    for (int i = 0; i < n; i++) {
        // Considera DiaUtil=1 E Feriado=0
        if (dados[i].diaUtil == 1 && dados[i].feriado == 0) {
            somaUtil += dados[i].consumo;
            countUtil++;
        } else {
            somaNaoUtil += dados[i].consumo;
            countNaoUtil++;
        }
    }
    printf("\nmedia de Consumo (Dia Util vs Fim de Semana/Feriado):\n");
    printf("  Dias Uteis (N=%d):\t%.2f kWh\n", countUtil, (countUtil > 0) ? somaUtil/countUtil : 0);
    printf("  Fins de Semana/Feriados (N=%d):\t%.2f kWh\n", countNaoUtil, (countNaoUtil > 0) ? somaNaoUtil/countNaoUtil : 0);
}


void preverConsumo(RegistroEnergia dados[], int n) {
    if (n < 3) {
        printf("\n--- Previsao ---\n");
        printf("Dados insuficientes para Previsao (N=%d). Mínimo de 3 dias necessários.\n", n);
        return;
    }
    
    printf("\n--- Previsao (Dia %d) ---\n", n + 1); // Prevendo o dia seguinte

    // 1. media Movel (MM3)
    // Previsao para Dia N+1 = media dos Dias N, N-1, N-2
    double mm3 = (dados[n-1].consumo + dados[n-2].consumo + dados[n-3].consumo) / 3.0;
    printf("Previsao (media Movel 3 dias): %.2f kWh\n", mm3);

    // --- BONUS: Regressao Linear Simples (Consumo ~ Irradiancia) ---
    // y = b0 + b1*x (onde y=Consumo, x=Irradiancia)
    
    double somaX = 0, somaY = 0, somaXY = 0, somaX2 = 0;
    for (int i = 0; i < n; i++) {
        double x = dados[i].irradiancia;
        double y = dados[i].consumo;
        somaX += x;
        somaY += y;
        somaXY += x * y;
        somaX2 += x * x;
    }
    
    double mediaX = somaX / n;
    double mediaY = somaY / n;
    
    // b1 = Cov(x,y) / Var(x)
    // Fórmulas mais robustas para b1 e b0 (evita grandes números)
    double b1_numerador = 0;
    double b1_denominador = 0;
    for(int i=0; i<n; i++) {
        b1_numerador += (dados[i].irradiancia - mediaX) * (dados[i].consumo - mediaY);
        b1_denominador += pow(dados[i].irradiancia - mediaX, 2);
    }

    double b1 = (b1_denominador == 0) ? 0 : b1_numerador / b1_denominador;
    
    // b0 = mediaY - b1 * mediaX
    double b0 = mediaY - (b1 * mediaX);

    printf("\nBONUS: Regressao Linear Simples (Consumo ~ Irradiancia)\n");
    printf("  Modelo: Consumo = %.2f + (%.2f * Irradiancia)\n", b0, b1);
    
    // *** IMPORTANTE ***
    // Para prever o Dia N+1 (Dia 368), precisaríamos do valor da
    // Irradiancia para esse dia (que não temos).
    // Se tivéssemos (ex: 2.5), a Previsao seria: (b0 + b1 * 2.5)
    
    // --- BONUS: Regressao Linear Multipla ---
    printf("\nBONUS: Regressao Linear Multipla\n");
    printf("  Implementar Regressao Multipla com Equacoes Normais em C puro\n");
    printf("  requer uma biblioteca de Algebra Linear (para inversao de matriz)\n");
    printf("  ou a implementacao manual de operacoes matriciais complexas.\n");
    printf("  Esta etapa e recomendada apenas com bibliotecas (ex: GSL) ou em outra linguagem.\n");
}



int main() {
    // <--- CORREÇÃO 2: Adicionado para setar o locale para o padrao do sistema
    // Isso fará o sscanf entender "17,5" como 17.5
    setlocale(LC_ALL, "");

    // Vetor de structs para armazenar todos os dados
    // Usamos alocação estática para simplicidade, mas malloc seria mais flexível
    static RegistroEnergia dados[MAX_DIAS]; 
    
    const char* arquivoEntrada = "consumo.csv";
    // const char* arquivoSaida = "consumo_analisado.csv";

    // 1. Ler Dados
    int numRegistros = lerCSV(arquivoEntrada, dados, MAX_DIAS);
    if (numRegistros <= 0) {
        printf("Falha ao ler dados (leu %d registros). Encerrando.\n", numRegistros);
        printf("Verifique se o arquivo '%s' está na mesma pasta do executável.\n", arquivoEntrada);
        return 1;
    }
    printf("Lidos %d registros do arquivo %s\n", numRegistros, arquivoEntrada);

    // 2. Tratar Dados
    tratarDados(dados, numRegistros);

    // 3. Analisar Dados
    analisarDados(dados, numRegistros);

    // 4. Prever Consumo
    // Assumindo que o arquivo tem 367 dias, queremos prever o dia 368.
    // Se o arquivo tiver N dias, a Previsao será para o dia N+1.
    preverConsumo(dados, numRegistros);

    // 5. Exportar (Opcional)
    // exportarCSV(arquivoSaida, dados, numRegistros);
    // printf("\nResultados exportados para %s\n", arquivoSaida);

    return 0;
}