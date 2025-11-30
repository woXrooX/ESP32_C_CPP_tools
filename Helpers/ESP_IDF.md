https://docs.espressif.com/projects/esp-idf/en/stable/esp32/index.html

1. Install the toolchain & ESP-IDF
	git clone --recursive https://github.com/espressif/esp-idf

2. Installs all dependencies (grabs xtensa-esp32-elf-gcc etc.)
	cd esp-idf && ./install.sh

3. Set environment variables for this shell (For this terminal window) (adds $IDF_PATH, $PATH etc.)
	NOTE: You need to be in installed path to run this (/Users/woxroox/ESP/esp-idf)
	. ./export.sh

4. Start a project
	idf.py create-project my_app
	cd my_app

5. Build
	idf.py build
	idf.py reconfigure build

6. Flash
	Let ESP-IDF auto-detect (when only one port exists) (omit -p)
	idf.py flash

	(Manual PORT)
	idf.py -p /dev/cu.usbserial-210 flash

7. Monitor (opens a serial monitor)
	idf.py monitor
	command + ] stops the loop




- Setting target
	(If you dont set the value, shows all options)
	idf.py set-target

	idf.py set-target esp32

- Finding USB
	1. List ports BEFORE plugging the board
	ls /dev/cu.* > /tmp/ports_before.txt

	2. Plug in the ESP32 board (wait ~2 s)

	3. List ports AFTER plugging it
	ls /dev/cu.* > /tmp/ports_after.txt

	4. Show the difference
	comm -13 /tmp/ports_before.txt /tmp/ports_after.txt




# Dependencies
idf.py add-dependency "espressif/esp_websocket_client^1.5.0"


// times: cycles (ON+OFF=1). times<=0 => infinite. interval_ms==0 => solid ON.
