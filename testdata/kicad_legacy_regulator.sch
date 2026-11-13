EESchema Schematic File Version 4
EELAYER 26 0
EELAYER END
$Descr A4 11693 8268
encoding utf-8
Sheet 1 1
Title "KiCad Legacy Demo Regulator"
Date "2026-11-09"
Rev "1.2"
Comp "Explorer2 Hardware"
$EndDescr
$Comp
L Device:R R1
U 1 1 61A00001
P 4250 3200
F 0 "R1" H 4250 3300 50  0000 C CNN
F 1 "10k" H 4250 3100 50  0000 C CNN
	1    4250 3200
	1    0    0    -1
$EndComp
$Comp
L Device:C C1
U 1 1 61A00002
P 5200 3200
F 0 "C1" H 5200 3300 50  0000 C CNN
F 1 "100nF" H 5200 3100 50  0000 C CNN
	1    5200 3200
	1    0    0    -1
$EndComp
$Comp
L Regulator_Linear:AMS1117-3.3 U1
U 1 1 61A00003
P 4725 2700
F 0 "U1" H 4725 2800 50  0000 C CNN
F 1 "AMS1117-3.3" H 4725 2600 50  0000 C CNN
	1    4725 2700
	1    0    0    -1
$EndComp
Wire Wire Line
	3800 3200 4250 3200
Wire Wire Line
	4250 3200 4725 2700
Wire Wire Line
	4725 2700 5200 3200
Wire Wire Line
	5200 3200 5700 3200
Connection ~ 4250 3200
Connection ~ 5200 3200
Text Notes 3800 3050 0    50   ~ 0
VCC_5V
Text Notes 5450 3050 0    50   ~ 0
VDD_3V3
$EndSCHEMATC