#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//MARCOS AGUSTIN GARCIA MOMBELLO

int main(){
    //Contador de palabras
    char frase[100];
    int contador = 0;
    printf("Ingrese una frase: ");
    fgets(frase, sizeof(frase), stdin);
    for(int i = 0; frase[i] != '\0'; i++){
        if(frase[i] == ' ' || frase[i] == '\n'){
            contador++;
        }
    }
    printf("La cantidad de palabras es: %d\n", contador);
    return 0;
}