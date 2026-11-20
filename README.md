Monitor de pulso y temperatura con ESP32
Prototipo de Bioingeniería de UNISANGIL. El Pulse Sensor analógico capta la onda de pulso (PPG) en el dedo y el ESP32 estima los latidos por minuto (BPM). La TFT ST7789V3 de 240 × 280 píxeles muestra la onda, las BPM y la temperatura superficial medida por el sensor infrarrojo GY-906 (MLX90614). Un pitido y un corazón animado marcan cada latido confirmado.
Conexiones
El montaje utiliza un ESP32 Dev Module con tarjeta de expansión. El Pulse Sensor, la TFT y el GY-906 se alimentan a 3,3 V. Todos los componentes comparten GND.
Componente	Terminal del módulo	Pin del ESP32
Pulse Sensor	S (salida analógica)	GPIO34
TFT ST7789V3	CS	GPIO5
TFT ST7789V3	DC	GPIO2
TFT ST7789V3	RST / RES	GPIO4
TFT ST7789V3	SCL / SCK (reloj SPI)	GPIO18
TFT ST7789V3	SDA / MOSI (datos SPI)	GPIO23
TFT ST7789V3	BLK (retroiluminación)	3V3
GY-906	SDA (datos I²C)	GPIO21
GY-906	SCL (reloj I²C)	GPIO22
Buzzer piezoeléctrico pasivo	Señal (+)	GPIO25
El diagrama de autoría propia (`diagramaconexiones.jpeg`) muestra el montaje con buzzer. La tabla incluye también el GY-906, con dirección I²C `0x5A`. Ver diagrama en Cirkit Designer.
Cargar y usar
En Arduino IDE, instala el paquete de placas `esp32` de Espressif Systems y selecciona ESP32 Dev Module. Guía de instalación de Espressif.
En el gestor de bibliotecas, instala PulseSensor Playground, Adafruit GFX Library, Adafruit ST7735 and ST7789 Library y Adafruit MLX90614 Library. Acepta las dependencias que proponga el gestor.
Abre `PulseSensor_TFT_Buzzer.ino`, selecciona el puerto USB y pulsa Subir. Al arrancar, deja el Pulse Sensor libre unos dos segundos para que mida el ruido de fondo.
Cuando aparezca «Coloque el dedo», apoya la yema con presión suave y mantenla quieta. Apunta el GY-906 hacia la palma para leer la temperatura en °C. El valor se actualiza cada segundo.
Abre el Serial Plotter a 115200 baudios para ver la señal filtrada `PPG` cada 20 ms. En la TFT, la escala se ajusta alrededor de la línea central para que los picos grandes quepan en pantalla.
Cómo se leen las BPM
Tres picos con dos intervalos parecidos dan la primera lectura. Después, el ESP32 promedia hasta seis intervalos recientes y, cuando tiene al menos tres, descarta el más corto y el más largo. Mientras sigue el pulso, la TFT espera al menos 2 segundos entre cambios del número. Si pasan 4 segundos sin un latido válido, muestra 0 BPM y la gráfica vuelve a la línea central.
En una simulación de un descenso sostenido de 87 a 75 BPM, la pantalla mostró 87 → 84 → 81 → 78 → 75 en unos ocho segundos. En las pruebas físicas, un recubrimiento opaco mejoró la continuidad de la onda en el dedo y permitió sostener la lectura en la oreja.
Carcasa
El diseño para impresión 3D separa la base del portapilas, la bandeja del ESP32 y la tapa con la pantalla y los sensores. El soporte del GY-906 lo orienta hacia la palma. La carcasa v1.2 mide 98 × 120 × 71 mm en el diseño y alcanza aproximadamente 74 mm de altura con los soportes.
Autores
Yeison Camilo Fonseca Chaparro, Jhon Daiver Chaparro Duccon y Kevin Perdomo.  
Ingeniería Electrónica · UNISANGIL · Bioingeniería.
