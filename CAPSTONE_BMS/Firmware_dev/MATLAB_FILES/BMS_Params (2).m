%% BMS_params.m  --  M0 simulation parameter file
%  Reconfigurable BMS capstone -- Milestone 0
%
%  PROVENANCE DISCIPLINE:
%  Every value below carries a label. Do not change a number without
%  updating its label. Labels used:
%     [VERIFIED]   read directly from a datasheet, page cited
%     [CALCULATED] derived from datasheet data, math documented
%     [ASSUMPTION] chosen by us -- must be swept or bench-tested
%     [OPEN]       not yet decided
%
%  Run this before opening the Simulink model.

clearvars -except od od_fix od_rec out; clc;
%% =====================================================================
%  CELL (struct: bat) -- Samsung INR18650-30Q
%  =====================================================================
%  Sources:
%    [SPEC]  "Spec. No. INR18650-30Q, Version 1.0"        (purchasing spec)
%    [INTRO] "Introduction of INR18650-30Q", Aug 2014     (characterisation)

bat.chemistry   = 'NCA';    % [VERIFIED] INTRO p.3
bat.V_max       = 4.20;     % V   [VERIFIED] INTRO p.3, CC-CV charge target
bat.V_min       = 2.50;     % V   [VERIFIED] INTRO p.3, discharge cut-off
bat.V_nom       = 3.61;     % V   [VERIFIED] INTRO p.3, typical
bat.I_max_cont  = 15.0;     % A   [VERIFIED] INTRO p.3
bat.Q_nom       = 2.953;    % Ah  [VERIFIED] INTRO p.6 table, 1C discharge

% Series resistance.
% [VERIFIED] INTRO p.3: DC-IR (10A-1A method) = 19.94 +/- 2 mOhm
% Cross-checked independently: our digitised-curve extraction gave
% 21.17 mOhm mean over 20-80% DoD, inside Samsung's spec band.
% NOTE: our extraction also produced a FALLING R below ~20% SOC. That is a
% self-heating artifact (1C cell ends at 33.7 C, 10A cell at 60.9 C --
% INTRO p.6), not real behaviour. Real R rises at low SOC. We therefore use
% the constant verified value rather than the extracted R(SOC) table.

% RC branch (charge dynamics) -- DELIBERATELY OMITTED.
% [CALCULATED] Steady-state extraction (~R0+R1) = 21.17 mOhm.
%              Samsung pulse DC-IR (~R0)        = 19.94 mOhm.
%              Implied R1 ~= 1.2 mOhm, about 6% of R0.
% Negligible over the minute-scale discharges M0 simulates, so no RC branch
% and no invented time constant. Revisit only if transient fidelity matters.
bat.R1          = NaN;      % [not used in M0]
bat.C1          = NaN;      % [not used in M0]

% OCV vs SOC lookup.
% [CALCULATED] From digitised discharge curves, INTRO p.6.
%   R(SOC)   = (V_1C - V_10A) / (10 - 3)
%   OCV(SOC) = V_1C + 3 * R(SOC)
% Endpoints anchored: SOC=1 -> 4.20 V, SOC=0 -> 2.50 V.
% *** PENDING BENCH VALIDATION ***  Datasheet SPEC 7.12 states cells ship at
% 3.620-3.690 V and calls that 40 +/- 5% SOC. This table maps that voltage
% band to 47.6-53.7% SOC -- roughly a 10-point discrepancy. Resolve in M1 by
% measuring a rested cell at a known coulomb-counted SOC.
% Table embedded inline so this file is self-contained (no CSV dependency).
%   columns: SOC (0-1) , OCV (V)
ocv_data = [ ...
%     SOC      OCV
    0.0000   2.5000 ; ...
    0.0200   2.7193 ; ...
    0.0400   2.8497 ; ...
    0.0600   2.9428 ; ...
    0.0800   3.0166 ; ...
    0.1000   3.0676 ; ...
    0.1200   3.1168 ; ...
    0.1400   3.1348 ; ...
    0.1600   3.1612 ; ...
    0.1800   3.1980 ; ...
    0.2000   3.2397 ; ...
    0.2200   3.2835 ; ...
    0.2400   3.3281 ; ...
    0.2600   3.3682 ; ...
    0.2800   3.4027 ; ...
    0.3000   3.4337 ; ...
    0.3200   3.4617 ; ...
    0.3400   3.4754 ; ...
    0.3600   3.4897 ; ...
    0.3800   3.5057 ; ...
    0.4000   3.5341 ; ...
    0.4200   3.5711 ; ...
    0.4400   3.5877 ; ...
    0.4600   3.6016 ; ...
    0.4800   3.6240 ; ...
    0.5000   3.6461 ; ...
    0.5200   3.6679 ; ...
    0.5400   3.6940 ; ...
    0.5600   3.7199 ; ...
    0.5800   3.7362 ; ...
    0.6000   3.7524 ; ...
    0.6200   3.7687 ; ...
    0.6400   3.7856 ; ...
    0.6600   3.8022 ; ...
    0.6800   3.8187 ; ...
    0.7000   3.8352 ; ...
    0.7200   3.8517 ; ...
    0.7400   3.8716 ; ...
    0.7600   3.8939 ; ...
    0.7800   3.9189 ; ...
    0.8000   3.9441 ; ...
    0.8200   3.9691 ; ...
    0.8400   3.9961 ; ...
    0.8600   4.0252 ; ...
    0.8800   4.0357 ; ...
    0.9000   4.0427 ; ...
    0.9200   4.0517 ; ...
    0.9400   4.0644 ; ...
    0.9600   4.0841 ; ...
    0.9800   4.1223 ; ...
    1.0000   4.2000 ; ...
];
bat.SOC_bp  = ocv_data(:,1);   % ascending, 0 -> 1
bat.OCV_tbl = ocv_data(:,2);   % V
bat.R0          = 0.020;
bat.R0_vec = bat.R0 * ones(size(bat.SOC_bp));   % ← SOC_bp doesn't exist yet
% Thermal reference points, for setting SOA thresholds later.
% [VERIFIED] INTRO p.6: measured cell surface temp at end of discharge.
bat.temp_vs_current = [ ...
%   I(A)   T_end(C)   Q(Ah)    t(min)
     3.0     33.7     2.953    59.1 ; ...
     5.0     41.0     2.950    35.4 ; ...
    10.0     60.9     2.945    17.7 ; ...
    15.0     81.2     2.917    11.7 ; ...
    20.0     99.4     2.879     8.6 ];

% [VERIFIED] SPEC p.3: discharge -20 to 75 C.
% Recommended re-discharge release below 60 C.
bat.T_discharge_max     = 75;   % C
bat.T_rederate_below    = 60;   % C

%% =====================================================================
%  SWITCH -- IRF4905 P-channel, back-to-back pairs
%  =====================================================================
%  Source: Infineon IRF4905PbF datasheet, Rev 2.1

fet.Rds_on    = 0.020;   % ohm [VERIFIED] p.1-2, max @ VGS=-10V, Tj=25C
fet.Vgs_th    = -3.0;    % V   [VERIFIED] p.2, range -2.0 to -4.0, midpoint
fet.Vf_body   = 1.3;     % V   [VERIFIED] p.3, body diode forward drop
fet.n_series  = 2;       %     back-to-back pair -> 2 devices conducting

% [CALCULATED] Conduction path resistance per string:
%   R_path = 2 * 0.020 = 0.040 ohm
% At 5 A per string: P = 5^2 * 0.040 = 1.00 W, V_drop = 0.20 V
% This is the loss that should make Scenario 1 come out SLIGHTLY WORSE for
% the reconfigurable topology. If it does not, the model is wrong.
fet.R_path    = fet.n_series * fet.Rds_on;

% *** DESIGN CONCERN, not a modelling detail ***
% Gate drive is 2N2222A pulling gate to ground, so VGS ~= -V_string.
%   3S at 4.2V/cell = 12.6 V -> VGS = -12.6 V   (past spec point, OK)
%   3S at 3.0V/cell =  9.0 V -> VGS =  -9.0 V   (UNDER-driven)
%   3S at 2.5V/cell =  7.5 V -> VGS =  -7.5 V   (well under)
% Rds_on rises when under-driven, i.e. exactly at low SOC when the
% reconfiguration logic is most active. Also has a strong positive tempco.
% [OPEN] pick a defensible worst-case Rds_on from the datasheet curves.

%% =====================================================================
%  PACK TOPOLOGY
%  =====================================================================

pack.S           = 3;    % cells in series per string
pack.P           = 2;    % parallel strings
pack.n_cells     = pack.S * pack.P;

% [CALCULATED] String resistance, healthy, 25 C:
%   R_string = 3 * 0.020 (cells) + 0.040 (switch pair) = 0.100 ohm
% Switches add ~67% on top of the cells' own 0.060 ohm.
pack.R_string    = pack.S * bat.R0 + fet.R_path;

pack.V_nom       = pack.S * bat.V_nom;   % 10.83 V
pack.V_max       = pack.S * bat.V_max;   % 12.60 V
pack.V_min       = pack.S * bat.V_min;   %  7.50 V

%% =====================================================================
%  SAFETY THRESHOLDS
%  =====================================================================
%  Source: SPEC "Pack Design Guideline (electrical)", NCA / E-Scooter column

th.V_cell_ov_trip     = 4.20;  % V [VERIFIED] SPEC 3.3 (cell absolute max)
                               %   NOTE: guideline recommends 4.10 V for this
                               %   application class. We use 4.20 V to
                               %   maximise demonstrable runtime on a
                               %   short-life POC pack. DOCUMENT THIS CHOICE.
th.V_cell_charge_resume = 4.05;% V [VERIFIED] pack design guideline
th.V_cell_uv_trip     = 2.50;  % V [VERIFIED] pack design guideline
th.V_cell_uv_warn     = 2.80;  % V [ASSUMPTION] margin above trip
th.V_bms_shutdown     = 2.00;  % V [VERIFIED] pack design guideline
th.V_no_charge_below  = 1.00;  % V [VERIFIED] cell is scrap, refuse charge

th.I_oc_trip = 12.0;  % A per string [DESIGN] Day 5. Below the 15 A cell
                      %   rating. Drive-cycle peak per string measured at
                      %   ~7.5 A, so 12 A rejects transients while still
                      %   protecting. Confirm against measured motor stall
                      %   current in M1.
th.T_ot_trip = 60.0;  % C [DESIGN] Day 5. SPEC recommends re-discharge
                      %   release below 60 C; INTRO p.6 shows 10 A
                      %   discharge reaching 60.9 C — i.e. exactly the
                      %   single-string fault-mode operating point.
                      %   VALIDATED: S2 isolated at 60.0 C vs 94.4 C for
                      %   the fixed baseline.th.dV_precharge_max   = 0.10;  % V     [ASSUMPTION] max dV to close parallel
th.t_cooldown         = 0.50;  % s     [ASSUMPTION] min between topology changes
th.R_ot_warn          = 50;    % C [ASSUMPTION] margin for early derate 
%% =====================================================================
%  MCU / SENSING -- what the firmware actually sees
%  =====================================================================
%  The control logic must NEVER read the simulator's true SOC. It reads a
%  coulomb-counted estimate built from the (quantised, noisy) current sensor.

mcu.T_loop        = 0.100;   % s   [ASSUMPTION] 100 ms control loop
mcu.adc_bits      = 12;      %     [VERIFIED] STM32F446RE
mcu.adc_vref      = 3.30;    % V   [ASSUMPTION] confirm on hardware
mcu.div_top       = 33e3;    % ohm [DESIGN] voltage divider
mcu.div_bot       = 10e3;    % ohm [DESIGN]
mcu.div_ratio     = mcu.div_bot / (mcu.div_top + mcu.div_bot);

% [CALCULATED] ADC resolution referred to the tap being measured:
%   LSB_adc  = 3.30 / 4096            = 0.806 mV
%   LSB_tap  = LSB_adc / div_ratio    = 3.47 mV
% Compare against th.dV_precharge_max = 100 mV -> ~29 LSB. Comfortable.
mcu.lsb_adc = mcu.adc_vref / 2^mcu.adc_bits;
mcu.lsb_tap = mcu.lsb_adc / mcu.div_ratio;

% [CALCULATED] Divider quiescent drain, both strings:
%   Per string taps at 4.2/8.4/12.6 V through 43 kOhm = 98+195+293 = 586 uA
%   Both strings = 1.17 mA
% Samsung's guideline is 10 uA/cell -> 60 uA for 6 cells. We are ~20x over.
% ACCEPTED for a POC. Document as a limitation. Use the kill switch between
% sessions: full string drains in ~213 days from dividers alone.
mcu.I_divider_total = 1.17e-3;  % A

mcu.soc_init_err  = 0.05;    % [ASSUMPTION] 5% initial SOC error, coulomb count
mcu.i_sensor_off  = 0.010;   % A [ASSUMPTION] INA226 offset -- sweep this

%% =====================================================================
%  SCENARIOS -- STILL OPEN
%  =====================================================================
% [OPEN] Load profile. Constant current, or a drive cycle with
%        accel / cruise / idle? This drives every M0 result.
%% ---- LOAD PROFILE ----
ld.I_base   = 0.5;    % A [ASSUMPTION] STM32 + INA226 + sensors
ld.I_cruise = 5.0;    % A [ASSUMPTION] 4x 12V gearmotor, pending motor spec
ld.I_accel  = 15.0;   % A [ASSUMPTION] startup, pending clamp measurement
ld.t_accel  = 0.5;    % s [ASSUMPTION] inrush duration, measure in M1
ld.t_cycle  = 60;     % s [ASSUMPTION] one accel + cruise period

% Constant-current profile -- used for the three main M0 scenarios
ld.I_const  = ld.I_cruise;

% Drive-cycle profile -- one extra run on Day 5 to show the logic
% survives transients. [t, I] pairs, zero-order hold.
n  = 60;                                 % number of cycles to generate
tc = ld.t_cycle; ta = ld.t_accel;
P  = [];
for k = 0:n-1
    t0 = k*tc;
    P = [P;
         t0            ld.I_accel;       % accelerate
         t0+ta         ld.I_cruise;      % cruise
         t0+tc-2       ld.I_base ];      % brief idle before next cycle
end
ld.profile = [P(:,1), P(:,2)];   % positive = discharge in Simscape convention
% [OPEN] Scenario 2: which cell faults, when, and how (thermal ramp?)
scen.fault_cell   = 2;      % String A, middle cell [DESIGN] tests tap subtraction
scen.fault_string = 'A';    % [DESIGN]

% Scenario 2a — overtemperature
scen.fault_type_a = 'overtemp';
scen.T_ambient    = 25;     % C  [ASSUMPTION]
scen.T_ramp_rate  = 5;      % C/min [VERIFIED-ish] SPEC 9.4 heating test uses 5 C/min
scen.fault_time   = 0.5;    % fraction of mission [DESIGN] per milestone plan

% Scenario 2b — undervoltage
scen.fault_type_b = 'undervolt';
scen.fault_soc_offset = -0.30;  % [ASSUMPTION] faulted cell starts 30% below


% Scenario 3: SOC imbalance between strings
scen.soc_imbalance  = 0.15;  % [DESIGN] 15%, per milestone plan

% [OPEN] Precharge resistor and bus capacitance. Both are design variables
% M0 should CHOOSE, not assume. Sweep them on Day 3.
scen.R_precharge     = 10;      % ohm  [DESIGN] Day 3 sweep — valid ONLY with load switch
scen.C_bus           = 1000e-6; % F    [ASSUMPTION] verify BTS7960 input caps in M1
scen.t_precharge     = 0.050;   % s    5*tau at R_pre=10, C=1000uF
pack.has_load_switch = true;    %      [DESIGN] REQUIRED — see Day 3 sweep

%% =====================================================================
%  CHARGING PARAMETERS
%  =====================================================================
chg.V_cell_full   = 4.18;    % V   [DESIGN] disconnect threshold, below 4.20 OV
chg.V_charger     = 12.6;    % V   [CALCULATED] 3 * 4.20, CC-CV charger limit
chg.R_charger     = 0.20;    % ohm [DESIGN] charger series R, limits ~3A at low SOC
chg.I_term        = 0.10;    % A   [ASSUMPTION] CC-CV termination current

%% =====================================================================
fprintf('BMS parameters loaded.\n');
fprintf('  Cell:   %.3f Ah, %.0f mOhm, OCV table %d points\n', ...
        bat.Q_nom, bat.R0*1e3, numel(bat.SOC_bp));
fprintf('  String: %dS, R_string = %.0f mOhm (cells %.0f + switches %.0f)\n', ...
        pack.S, pack.R_string*1e3, pack.S*bat.R0*1e3, fet.R_path*1e3);
fprintf('  ADC:    %.2f mV per LSB at the tap\n', mcu.lsb_tap*1e3);
fprintf('  OPEN items remain -- see [OPEN] tags above.\n');

%% ---- PARAMETER VALIDATION ----
assert(scen.R_precharge > 0, 'R_precharge must be positive');
assert(scen.C_bus > 0, 'C_bus must be positive');
assert(numel(bat.SOC_bp) == numel(bat.OCV_tbl), 'OCV table length mismatch');
assert(issorted(bat.SOC_bp), 'SOC breakpoints must be ascending');
assert(bat.V_min < bat.V_max, 'Voltage limits inverted');