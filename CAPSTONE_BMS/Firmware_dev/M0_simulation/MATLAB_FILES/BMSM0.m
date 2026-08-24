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
%  CELL -- Samsung INR18650-30Q
%  =====================================================================
%  Sources:
%    [SPEC]  "Spec. No. INR18650-30Q, Version 1.0"        (purchasing spec)
%    [INTRO] "Introduction of INR18650-30Q", Aug 2014     (characterisation)

cell.chemistry   = 'NCA';    % [VERIFIED] INTRO p.3
cell.V_max       = 4.20;     % V   [VERIFIED] INTRO p.3, CC-CV charge target
cell.V_min       = 2.50;     % V   [VERIFIED] INTRO p.3, discharge cut-off
cell.V_nom       = 3.61;     % V   [VERIFIED] INTRO p.3, typical
cell.I_max_cont  = 15.0;     % A   [VERIFIED] INTRO p.3
cell.Q_nom       = 2.953;    % Ah  [VERIFIED] INTRO p.6 table, 1C discharge

% Series resistance.
% [VERIFIED] INTRO p.3: DC-IR (10A-1A method) = 19.94 +/- 2 mOhm
% Cross-checked independently: our digitised-curve extraction gave
% 21.17 mOhm mean over 20-80% DoD, inside Samsung's spec band.
% NOTE: our extraction also produced a FALLING R below ~20% SOC. That is a
% self-heating artifact (1C cell ends at 33.7 C, 10A cell at 60.9 C --
% INTRO p.6), not real behaviour. Real R rises at low SOC. We therefore use
% the constant verified value rather than the extracted R(SOC) table.
cell.R0          = 0.020;    % ohm [VERIFIED] INTRO p.3

% RC branch (charge dynamics) -- DELIBERATELY OMITTED.
% [CALCULATED] Steady-state extraction (~R0+R1) = 21.17 mOhm.
%              Samsung pulse DC-IR (~R0)        = 19.94 mOhm.
%              Implied R1 ~= 1.2 mOhm, about 6% of R0.
% Negligible over the minute-scale discharges M0 simulates, so no RC branch
% and no invented time constant. Revisit only if transient fidelity matters.
cell.R1          = NaN;      % [not used in M0]
cell.C1          = NaN;      % [not used in M0]

% OCV vs SOC lookup.
% [CALCULATED] From digitised discharge curves, INTRO p.6.
%   R(SOC)   = (V_1C - V_10A) / (10 - 3)
%   OCV(SOC) = V_1C + 3 * R(SOC)
% Endpoints anchored: SOC=1 -> 4.20 V, SOC=0 -> 2.50 V.
% *** PENDING BENCH VALIDATION ***  Datasheet SPEC 7.12 states cells ship at
% 3.620-3.690 V and calls that 40 +/- 5% SOC. This table maps that voltage
% band to 47.6-53.7% SOC -- roughly a 10-point discrepancy. Resolve in M1 by
% measuring a rested cell at a known coulomb-counted SOC.
tbl = readtable('30Q_OCV_R_table.csv');
cell.SOC_bp  = tbl.SOC;         % ascending, 0 -> 1
cell.OCV_tbl = tbl.OCV_V;       % V
cell.R_tbl   = tbl.R_series_ohm;% ohm -- reference only, NOT used (see above)

% Thermal reference points, for setting SOA thresholds later.
% [VERIFIED] INTRO p.6: measured cell surface temp at end of discharge.
cell.temp_vs_current = [ ...
%   I(A)   T_end(C)   Q(Ah)    t(min)
     3.0     33.7     2.953    59.1 ; ...
     5.0     41.0     2.950    35.4 ; ...
    10.0     60.9     2.945    17.7 ; ...
    15.0     81.2     2.917    11.7 ; ...
    20.0     99.4     2.879     8.6 ];

% [VERIFIED] SPEC p.3: discharge -20 to 75 C.
% Recommended re-discharge release below 60 C.
cell.T_discharge_max     = 75;   % C
cell.T_rederate_below    = 60;   % C

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
pack.R_string    = pack.S * cell.R0 + fet.R_path;

pack.V_nom       = pack.S * cell.V_nom;   % 10.83 V
pack.V_max       = pack.S * cell.V_max;   % 12.60 V
pack.V_min       = pack.S * cell.V_min;   %  7.50 V

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

th.I_oc_trip          = NaN;   % A     [OPEN] set on Day 5 from scenarios
th.T_ot_trip          = NaN;   % C     [OPEN] see cell.temp_vs_current
th.dV_precharge_max   = 0.10;  % V     [ASSUMPTION] max dV to close parallel
th.t_cooldown         = 0.50;  % s     [ASSUMPTION] min between topology changes

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
scen.load_profile   = [];    % [OPEN]

% [OPEN] Scenario 2: which cell faults, when, and how (thermal ramp?)
scen.fault_cell     = [];    % [OPEN]
scen.fault_time     = [];    % [OPEN]

% Scenario 3: SOC imbalance between strings
scen.soc_imbalance  = 0.15;  % [DESIGN] 15%, per milestone plan

% [OPEN] Precharge resistor and bus capacitance. Both are design variables
% M0 should CHOOSE, not assume. Sweep them on Day 3.
scen.R_precharge    = NaN;   % ohm [OPEN]
scen.C_bus          = NaN;   % F   [OPEN]

%% =====================================================================
fprintf('BMS parameters loaded.\n');
fprintf('  Cell:   %.3f Ah, %.0f mOhm, OCV table %d points\n', ...
        cell.Q_nom, cell.R0*1e3, numel(cell.SOC_bp));
fprintf('  String: %dS, R_string = %.0f mOhm (cells %.0f + switches %.0f)\n', ...
        pack.S, pack.R_string*1e3, pack.S*cell.R0*1e3, fet.R_path*1e3);
fprintf('  ADC:    %.2f mV per LSB at the tap\n', mcu.lsb_tap*1e3);
fprintf('  OPEN items remain -- see [OPEN] tags above.\n');