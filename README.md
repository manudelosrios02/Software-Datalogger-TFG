# Sistema de adquisición y monitorización de medidas eléctricas

Este proyecto implementa un sistema de **adquisición, registro y almacenamiento de medidas eléctricas** de tensión, corriente, potencia y energía para una **instalación fotovoltaica autónoma**.

El sistema utiliza **dos sensores INA226** y un **medidor de energía PZEM-004T**, almacenando los datos en una **tarjeta microSD en formato CSV**, con generación automática de archivos numerados para evitar el solapamiento entre sesiones de medida.

## Características principales
- Medida de tensión, corriente, potencia y energía
- Registro de datos en tarjeta microSD
- Archivos CSV numerados automáticamente
- Comunicación mediante I2C, UART y SPI
- Diseño modular preparado para futuras ampliaciones (WiFi)

## Hardware utilizado
- **Arduino UNO R4** (Renesas RA4M1)
- **2 × INA226** – sensores de monitorización (bus I2C)
- **PZEM-004T v3.0** – medidor de energía (UART / Serial1)
- **Módulo microSD** – almacenamiento de datos (SPI)

## Comunicaciones
- **I2C**: sensores INA226  
- **UART (Serial1)**: PZEM-004T v3.0  
- **SPI**: tarjeta microSD  

## Funcionamiento
Al iniciarse el sistema:
1. Se inicializan los sensores y los buses de comunicación.
2. Se crea un nuevo archivo CSV en la tarjeta microSD con numeración automática.
3. Se adquieren periódicamente las medidas eléctricas.
4. Los datos se almacenan en el archivo CSV para su posterior análisis.

Cada sesión de medida genera un archivo independiente, evitando la sobrescritura de datos.

## Formato de los datos
Los datos se almacenan en **formato CSV**, lo que permite su análisis posterior mediante herramientas como MATLAB, Excel o Python.

## Librerías utilizadas
- **INA226 Library**  
  Autor: Rob Tillaart  
  https://github.com/RobTillaart/INA226  
  Licencia: MIT

- **PZEMPlus Library**  
  Autor: Oleksandr "olehs"  
  https://github.com/olehs/PZEM004T  
  Licencia: GPL v3.0

- **SD Library (Arduino)**  
  Licencia: LGPL 2.1 / BSD

- **Wire.h**  
  Licencia: LGPL 2.1

- **SPI.h**  
  Licencia: LGPL 2.1

- **WiFiS3 Library**  
  Licencia: LGPL 2.1

## Posibles ampliaciones
- Envío de datos mediante WiFi
- Integración con servidor remoto o plataforma IoT
- Visualización en tiempo real
- Gestión avanzada de energía

## Autor
**Manuel de los Ríos Piosa**  
Proyecto: *Diseño y construcción de módulo de medición y monitoreo para instalación fotovoltaica autónoma*  
Versión: 3.0  
Fecha: 14/01/2026
