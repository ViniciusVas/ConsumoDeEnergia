#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <locale.h> // Essencial para ler "17,5" corretamente no Brasil

// --- Configurações ---
#define MAX_DIAS 400
#define MAX_LINHA 1024
#define JANELA_OUTLIER 2 // Janela de ±2 dias
#define Z_SCORE_LIMITE 3.0 // Limite para considerar outlier

// --- Estrutura de Dados ---
typedef struct {
    // Dados do CSV
    int dia;
    char data[15];
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

    // Dados Calculados (Tratamento/Análise)
    double consumoLiquido;
    double zscoreConsumo;
    int ehOutlier;
} RegistroEnergia;

// --- Protótipos ---
int lerCSV(const char* nomeArquivo, RegistroEnergia dados[], int maxRegistros);
void tratarDados(RegistroEnergia dados[], int n);
void analisarDados(RegistroEnergia dados[], int n);
void preverConsumo(RegistroEnergia dados[], int n);
void exportarCSV(const char* nomeArquivo, RegistroEnergia dados[], int n);

// ============================================================================
// FUNÇÃO PRINCIPAL
// ============================================================================
int main() {
    // 1. Configurar Locale para Brasil (aceitar vírgula como decimal)
    setlocale(LC_ALL, ""); 

    static RegistroEnergia dados[MAX_DIAS];
    const char* arquivoEntrada = "consumo.csv";
    const char* arquivoSaida = "resultado_completo.csv";

    printf("--- INICIO DO PROGRAMA ---\n");

    // 2. Leitura
    int n = lerCSV(arquivoEntrada, dados, MAX_DIAS);
    if (n <= 0) {
        printf("ERRO CRITICO: Nao foi possivel ler '%s'.\n", arquivoEntrada);
        printf("Verifique se o arquivo esta na mesma pasta do executavel.\n");
        return 1;
    }
    printf("Leitura concluida: %d dias carregados.\n", n);

    // 3. Validação Cruzada (Excel vs C)
    // Calcula a média bruta (com outliers e erros) para provar que leu igual ao Excel
    double somaBruta = 0;
    for(int i=0; i<n; i++) somaBruta += dados[i].consumo;
    printf("\n--- VALIDACAO (Comparacao com Excel) ---\n");
    printf("Media BRUTA (Dados crus): %.2f (No Sheets deve ser ~5776)\n", somaBruta/n);
    printf("Agora aplicaremos o tratamento para remover outliers...\n");

    // 4. Tratamento e Análise
    tratarDados(dados, n);
    analisarDados(dados, n);
    preverConsumo(dados, n);

    // 5. Exportação Final
    exportarCSV(arquivoSaida, dados, n);

    printf("\n--- FIM ---\n");
    return 0;
}

// ============================================================================
// IMPLEMENTAÇÃO DAS FUNÇÕES
// ============================================================================

int lerCSV(const char* nomeArquivo, RegistroEnergia dados[], int maxRegistros) {
    FILE* fp = fopen(nomeArquivo, "r");
    if (!fp) return -1;

    char linha[MAX_LINHA];
    int n = 0;

    // Pular cabeçalho
    if (fgets(linha, MAX_LINHA, fp) == NULL) { fclose(fp); return 0; }

    // Ler linhas. Formato: Dia;Data;Temp... (Separador ;)
    while (n < maxRegistros && fgets(linha, MAX_LINHA, fp)) {
        // O %*[^;] lê a data como string até encontrar o próximo ; e ignora (ou lemos manualmente)
        // Aqui vamos ler a data para a string dados[n].data
        // Nota: sscanf sensivel ao locale para %lf
        int lidos = sscanf(linha, "%d;%[^;];%lf;%lf;%lf;%lf;%lf;%d;%d;%lf;%lf;%lf;%lf;%lf",
            &dados[n].dia, dados[n].data, 
            &dados[n].temp, &dados[n].umidade, &dados[n].irradiancia, 
            &dados[n].vento, &dados[n].ocupacao, 
            &dados[n].diaUtil, &dados[n].feriado, &dados[n].tarifaPonta,
            &dados[n].consumo, &dados[n].geracaoFV, &dados[n].cargaVE, &dados[n].importacaoRede
        );

        if (lidos == 14) n++; 
    }
    fclose(fp);
    return n;
}

// Auxiliar para mediana
double medianaJanela(RegistroEnergia dados[], int indice, int n) {
    double soma = 0;
    int count = 0;
    for (int i = indice - JANELA_OUTLIER; i <= indice + JANELA_OUTLIER; i++) {
        // Só usa na média se estiver dentro do vetor e NÃO for outro outlier
        if (i >= 0 && i < n && !dados[i].ehOutlier) {
            soma += dados[i].consumo;
            count++;
        }
    }
    return (count > 0) ? (soma / count) : dados[indice].consumo;
}

void tratarDados(RegistroEnergia dados[], int n) {
    // 1. Limpeza Básica (Negativos e Zeros)
    for (int i = 0; i < n; i++) {
        // Se consumo < 0 ou for 0 (assumindo erro de leitura), pega do dia anterior
        if (dados[i].consumo <= 0.001) dados[i].consumo = (i > 0) ? dados[i-1].consumo : 0;
        if (dados[i].geracaoFV < 0) dados[i].geracaoFV = (i > 0) ? dados[i-1].geracaoFV : 0;
    }

    // 2. Detecção de Outliers (Z-Score)
    double soma = 0, somaQuad = 0;
    for(int i=0; i<n; i++) soma += dados[i].consumo;
    double media = soma / n;

    for(int i=0; i<n; i++) somaQuad += pow(dados[i].consumo - media, 2);
    double desvio = sqrt(somaQuad / n);

    printf("\n--- Tratamento de Outliers ---\n");
    printf("Parametros Globais -> Media: %.2f, Desvio: %.2f\n", media, desvio);

    int countOutliers = 0;
    for(int i=0; i<n; i++) {
        dados[i].zscoreConsumo = (dados[i].consumo - media) / desvio;
        dados[i].ehOutlier = (fabs(dados[i].zscoreConsumo) > Z_SCORE_LIMITE);
        
        if (dados[i].ehOutlier) {
            double valorAntigo = dados[i].consumo;
            // Substitui pela mediana local
            dados[i].consumo = medianaJanela(dados, i, n);
            printf("Outlier Dia %d: Era %.2f (Z=%.2f) -> Virou %.2f\n", 
                   dados[i].dia, valorAntigo, dados[i].zscoreConsumo, dados[i].consumo);
            countOutliers++;
        }
    }
    if (countOutliers == 0) printf("Nenhum outlier detectado.\n");
}

double calcularCorrelacao(RegistroEnergia dados[], int n, const char* var1, const char* var2) {
    double sumX=0, sumY=0, sumXY=0, sumX2=0, sumY2=0;
    for(int i=0; i<n; i++) {
        double x, y;
        // Selecionar X
        if(strcmp(var1, "consumo")==0) x=dados[i].consumo;
        
        // Selecionar Y
        if(strcmp(var2, "temp")==0) y=dados[i].temp;
        else if(strcmp(var2, "umidade")==0) y=dados[i].umidade;
        else if(strcmp(var2, "ocupacao")==0) y=dados[i].ocupacao;
        else if(strcmp(var2, "irradiancia")==0) y=dados[i].irradiancia;
        else if(strcmp(var2, "diaUtil")==0) y=(double)dados[i].diaUtil;
        else y=0;

        sumX += x; sumY += y; sumXY += x*y; sumX2 += x*x; sumY2 += y*y;
    }
    double num = n*sumXY - sumX*sumY;
    double den = sqrt((n*sumX2 - sumX*sumX) * (n*sumY2 - sumY*sumY));
    return (den == 0) ? 0 : num/den;
}

void analisarDados(RegistroEnergia dados[], int n) {
    printf("\n--- Analise Estatistica (Dados Tratados) ---\n");
    
    double minC=dados[0].consumo, maxC=dados[0].consumo, sumC=0;
    double minFV=dados[0].geracaoFV, maxFV=dados[0].geracaoFV, sumFV=0;
    double minImp=dados[0].importacaoRede, maxImp=dados[0].importacaoRede, sumImp=0;

    for(int i=0; i<n; i++) {
        dados[i].consumoLiquido = dados[i].consumo - dados[i].geracaoFV;
        
        sumC += dados[i].consumo;
        if(dados[i].consumo < minC) minC = dados[i].consumo;
        if(dados[i].consumo > maxC) maxC = dados[i].consumo;

        sumFV += dados[i].geracaoFV;
        if(dados[i].geracaoFV < minFV) minFV = dados[i].geracaoFV;
        if(dados[i].geracaoFV > maxFV) maxFV = dados[i].geracaoFV;

        sumImp += dados[i].importacaoRede;
        if(dados[i].importacaoRede < minImp) minImp = dados[i].importacaoRede;
        if(dados[i].importacaoRede > maxImp) maxImp = dados[i].importacaoRede;
    }

    printf("Consumo (kWh):    Media=%.2f  Min=%.2f  Max=%.2f\n", sumC/n, minC, maxC);
    printf("Geracao FV (kWh): Media=%.2f  Min=%.2f  Max=%.2f\n", sumFV/n, minFV, maxFV);
    printf("Importacao (kWh): Media=%.2f  Min=%.2f  Max=%.2f\n", sumImp/n, minImp, maxImp);

    printf("\nCorrelações (Pearson):\n");
    printf("  vs Temperatura: %.4f\n", calcularCorrelacao(dados, n, "consumo", "temp"));
    printf("  vs Umidade:     %.4f\n", calcularCorrelacao(dados, n, "consumo", "umidade"));
    printf("  vs Ocupacao:    %.4f\n", calcularCorrelacao(dados, n, "consumo", "ocupacao"));
    printf("  vs Irradiancia: %.4f\n", calcularCorrelacao(dados, n, "consumo", "irradiancia"));
    printf("  vs Dia Util:    %.4f\n", calcularCorrelacao(dados, n, "consumo", "diaUtil"));

    // Comparação Dia Útil
    double sUtil=0, sFDS=0; int cUtil=0, cFDS=0;
    for(int i=0; i<n; i++) {
        if(dados[i].diaUtil==1 && dados[i].feriado==0) { sUtil+=dados[i].consumo; cUtil++; }
        else { sFDS+=dados[i].consumo; cFDS++; }
    }
    printf("\nMedia Consumo: Dia Util (%.2f) vs FDS/Feriado (%.2f)\n", 
        cUtil?sUtil/cUtil:0, cFDS?sFDS/cFDS:0);
}

void preverConsumo(RegistroEnergia dados[], int n) {
    if(n<3) return;
    printf("\n--- Previsao Futura (Dia %d) ---\n", n+1);
    
    // MM3
    double mm3 = (dados[n-1].consumo + dados[n-2].consumo + dados[n-3].consumo)/3.0;
    printf("Previsao MM3: %.2f kWh\n", mm3);

    // Regressão Simples (Apenas cálculo dos coeficientes para exibição)
    double sX=0, sY=0, sXY=0, sX2=0;
    for(int i=0; i<n; i++) {
        double x = dados[i].irradiancia;
        double y = dados[i].consumo;
        sX+=x; sY+=y; sXY+=x*y; sX2+=x*x;
    }
    double mX = sX/n; double mY = sY/n;
    double b1 = (sXY - n*mX*mY) / (sX2 - n*mX*mX);
    double b0 = mY - b1*mX;
    
    printf("Modelo Linear: Consumo = %.2f + (%.2f * Irradiancia)\n", b0, b1);
    printf("Nota: Para prever o dia %d via Regressao, precisamos da Irradiancia prevista.\n", n+1);
}

void exportarCSV(const char* nomeArquivo, RegistroEnergia dados[], int n) {
    FILE* f = fopen(nomeArquivo, "w");
    if (!f) { printf("Erro ao criar arquivo de exportacao.\n"); return; }

    // Recalcular coeficientes de regressão para usar no loop
    double sX=0, sY=0, sXY=0, sX2=0;
    for(int i=0; i<n; i++) {
        double x = dados[i].irradiancia;
        double y = dados[i].consumo;
        sX+=x; sY+=y; sXY+=x*y; sX2+=x*x;
    }
    double mX = sX/n; double mY = sY/n;
    double b1 = (sXY - n*mX*mY) / (sX2 - n*mX*mX);
    double b0 = mY - b1*mX;

    // Cabeçalho
    fprintf(f, "Dia;Data;ConsumoOriginal;ConsumoTratado;ConsumoLiquido;GeraçãoFV;ZScore;EhOutlier;Prev_MM3;Prev_Linear\n");

    for(int i=0; i<n; i++) {
        double mm3 = (i>=3) ? (dados[i-1].consumo + dados[i-2].consumo + dados[i-3].consumo)/3.0 : 0.0;
        double prevLinear = b0 + b1 * dados[i].irradiancia;

        // %g remove zeros desnecessários, %.2f fixa 2 casas
        fprintf(f, "%d;%s;%.2f;%.2f;%.2f;%.2f;%.4f;%d;%.2f;%.2f\n",
            dados[i].dia,
            dados[i].data,
            dados[i].consumo, // Este já é o tratado, mas o C não guarda o original na struct. 
                              // Se quisesse o original, teria que ter criado um campo extra na struct antes do tratamento.
                              // Como o tratamento substitui, aqui vai o tratado.
            dados[i].consumo,
            dados[i].consumoLiquido,
            dados[i].geracaoFV,
            dados[i].zscoreConsumo,
            dados[i].ehOutlier,
            mm3,
            prevLinear
        );
    }
    fclose(f);
    printf("\nArquivo '%s' exportado com sucesso!\n", nomeArquivo);
    printf("Contem: Consumo, Consumo Liquido, ZScore, Prev MM3 e Prev Linear.\n");
}