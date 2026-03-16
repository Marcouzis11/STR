#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void cuenta_regresiva(int segundos);

int main(){
    int semaforoRojo = 1;
    int semaforoAmarillo = 0;
    int semaforoVerde = 0;

    while(1){

        if(semaforoRojo == 1){
            semaforoRojo = 0;
            semaforoAmarillo = 1;
            printf("[AMARILLO]\n");
            cuenta_regresiva(5);
            
        }
        else if(semaforoAmarillo == 1){
            semaforoAmarillo = 0;
            semaforoVerde = 1;
            printf("[VERDE]\n");
            cuenta_regresiva(10);
        }
        else if(semaforoVerde == 1){
            semaforoVerde = 0;
            semaforoRojo = 1;
            printf("[ROJO]\n");
            cuenta_regresiva(15);
        }
    }
}


void cuenta_regresiva(int segundos) {
    while (segundos > 0) {
        printf("Tiempo restante: %d segundos\n", segundos);
        fflush(stdout);  // fuerza a imprimir en tiempo real
        sleep(1);
        segundos--;
    }
}