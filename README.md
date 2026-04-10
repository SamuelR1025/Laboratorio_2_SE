#Laboratorio 2: Juego con Matrix 8x8 y ESP32

Este proyecto consiste en la implementación del clásico juego Pong utilizando un microcontrolador ESP32 y una Matriz LED de 8x8 bicolor. El sistema integra control de hardware (multiplexación y transistores), lógica de estados de juego y manejo de periféricos mediante interrupciones y temporizadores.

🚀 Características
Juego funcional: Sistema de 2 jugadores con raquetas y pelota con físicas de rebote.

Multiplexación de alta velocidad: Tasa de refresco de 800Hz para una visualización sin parpadeos.

Lógica de Potencia: Uso de 8 transistores PNP (2N3906) para el control de corriente de la matriz.

Optimización de Hardware: Implementación de resistencias Pull-up internas del ESP32 para los controles, eliminando la necesidad de componentes externos.

Sistema de Animaciones: Pantallas de puntaje, celebración de goles y pantalla de victoria personalizada con parpadeo selectivo.
