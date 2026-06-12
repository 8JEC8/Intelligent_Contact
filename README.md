# Intelligent_Contact

https://www.youtube.com/watch?v=sm2dUYbs29c

Projecto de 8vo semestre de Ingeniería en Electrónica en el Tecnológico de Monterrey, Campus Monterrey.

El dispositivo consiste en un contacto inteligente capaz de monitorear los parámetros eléctricos de una carga (V, I, FP, etc.) y también controlar el estado de la misma. El control consiste en la actuación del contacto (permitir o negar operación mediante un SSR) para así garantizar la seguridad del equipo ante anomalías como cambios bruscos en demanda de corriente y cambios en factor de potencia; apagándose automáticamente al no estar en umbrales especificados por el usuario.

La utilización de comunicación inalámbrica mediante LoRa permite una operación a mayores distancias comparado con el WiFi. Por esta razón es que este dispositivo está pensado en ambientes industriales o comerciales en los que se tienen edificios con varias instalaciones que desean monitorear el estado del equipo eléctrico de una manera conveniente y segura. La interfaz de usuario en la estación fue desarrollada en HTML y se puede acceder a ella mediante WiFi a un WebServer creado por la ESP32; esto permitiría conectar este ESP32 a la red WiFi de un edificio y permitir al personal acceder a la información convenientemente y monitorear/controlar contactos en edificios que se encuentran hasta el otro lado de las instalaciones.

Knicks in 5.
