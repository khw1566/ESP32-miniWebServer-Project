# M4 power status

JC1060P470C battery divider/charger telemetry wiring is not confirmed by the
provided reference. m4_power_init does not access GPIO53 or any charger register;
m4_power_poll reports all known flags false. UI power status remains unknown.

M3 GPIO53 resistor values, IP5306 state estimates and case/power drawings do
not establish M4 wiring. Keep external power, charging indication and measured
battery-current direction separate. Board-specific circuit evidence is required
before implementing battery telemetry.
