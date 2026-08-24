function [cmd_mainA, cmd_mainB, cmd_load, state_out, fault_out] = ...
         bms_soa_pass2(tapsA, tapsB, socA, socB)
%BMS_SOA_PASS2  Safe-operating-area layer for reconfigurable 3S2P BMS.
%
%  MILESTONE 0 -- PASS 2.  Safety checks only. No topology optimisation,
%  no precharge sequencing, no cooldown. Those are Pass 3.
%
%  DESIGN RULE (from project brief): SOA checks run BEFORE any efficiency
%  or balancing logic. A string that violates SOA is isolated regardless
%  of what the SOC logic would prefer.
%
%  INPUTS
%    tapsA, tapsB  3x1  quantised tap voltages, referenced to pack negative.
%                       These are what the ADC actually returns after the
%                       33k/10k divider. Per-cell voltage is NOT measured
%                       directly -- it is reconstructed below by subtraction,
%                       exactly as the STM32 firmware will do it.
%    socA, socB    1x1  coulomb-counted SOC ESTIMATES. Never the simulator's
%                       true SOC -- the MCU cannot know that.
%
%  OUTPUTS
%    cmd_mainA/B   0/1  main switch pair command per string
%    cmd_load      0/1  load disconnect switch
%    state_out          see state codes below
%    fault_out          bitfield, see fault codes below
%
%  NOTE ON ISOLATED STRINGS: the tap dividers are permanently connected to
%  the cells, so an isolated string still reports valid (open-circuit) tap
%  voltages. This mirrors the hardware and means we can keep monitoring a
%  string we have disconnected.

%% ---- CONSTANTS -----------------------------------------------------
% KEEP IN SYNC WITH BMS_params.m. A MATLAB Function block cannot read the
% base workspace, so these are duplicated. If you change a threshold there,
% change it here too.
V_UV_TRIP   = 2.50;   % V  [VERIFIED] SPEC pack design guideline, NCA
V_OV_TRIP   = 4.20;   % V  [VERIFIED] SPEC 3.3, cell absolute max
V_UV_WARN   = 2.80;   % V  [ASSUMPTION] margin above trip
DEBOUNCE_N  = 3;      %    [ASSUMPTION] consecutive samples before tripping
                      %    3 samples at 100 ms = 300 ms. Long enough to
                      %    reject a single quantisation flicker, short
                      %    enough to act well before cell damage.

%% ---- STATE CODES ---------------------------------------------------
S_INIT      = 0;
S_BOTH      = 1;   % both strings carrying load
S_ISOLATE_A = 2;   % A faulted, B carries load
S_ISOLATE_B = 3;   % B faulted, A carries load
S_SHUTDOWN  = 4;   % both faulted -- LATCHED, no auto-recovery

%% ---- FAULT BITS ----------------------------------------------------
F_NONE   = 0;
F_UV_A   = 1;    % bit 0
F_UV_B   = 2;    % bit 1
F_OV_A   = 4;    % bit 2
F_OV_B   = 8;    % bit 3

%% ---- PERSISTENT STATE ----------------------------------------------
% These survive between 100 ms calls, exactly like static variables in C.
persistent state fault uvA_cnt uvB_cnt ovA_cnt ovB_cnt

if isempty(state)
    % POWER-UP DEFAULT: all switches OFF.
    % Mirrors the 100k base pull-downs on the 2N2222A gate drivers -- on
    % reset or brownout the hardware defaults to open, and so does this.
    state   = S_INIT;
    fault   = F_NONE;
    uvA_cnt = 0;  uvB_cnt = 0;
    ovA_cnt = 0;  ovB_cnt = 0;
end

%% ---- RECONSTRUCT PER-CELL VOLTAGES ---------------------------------
% The firmware does this same subtraction. Measured error is ~0.4 mV
% typical, bounded near 7 mV worst case (2 LSB at 3.46 mV/LSB).
vA = [ tapsA(1);
       tapsA(2) - tapsA(1);
       tapsA(3) - tapsA(2) ];

vB = [ tapsB(1);
       tapsB(2) - tapsB(1);
       tapsB(3) - tapsB(2) ];

%% ---- SOA CHECKS, WITH DEBOUNCE -------------------------------------
% Debounce matters because a single sample straddling a quantisation
% boundary can read one LSB low. Tripping on that would cause spurious
% isolation -- the "false triggers will ruin the demo" failure mode.

uvA_now = any(vA < V_UV_TRIP);
uvB_now = any(vB < V_UV_TRIP);
ovA_now = any(vA > V_OV_TRIP);
ovB_now = any(vB > V_OV_TRIP);

if uvA_now, uvA_cnt = uvA_cnt + 1; else, uvA_cnt = 0; end
if uvB_now, uvB_cnt = uvB_cnt + 1; else, uvB_cnt = 0; end
if ovA_now, ovA_cnt = ovA_cnt + 1; else, ovA_cnt = 0; end
if ovB_now, ovB_cnt = ovB_cnt + 1; else, ovB_cnt = 0; end

% Faults LATCH. Once a string has violated SOA we do not re-close it on
% the strength of a later good reading -- an unloaded cell recovers voltage
% and would look healthy again, causing oscillation.
if uvA_cnt >= DEBOUNCE_N, fault = bitor(fault, F_UV_A); end
if uvB_cnt >= DEBOUNCE_N, fault = bitor(fault, F_UV_B); end
if ovA_cnt >= DEBOUNCE_N, fault = bitor(fault, F_OV_A); end
if ovB_cnt >= DEBOUNCE_N, fault = bitor(fault, F_OV_B); end

faultA = bitand(fault, bitor(F_UV_A, F_OV_A)) ~= 0;
faultB = bitand(fault, bitor(F_UV_B, F_OV_B)) ~= 0;

%% ---- STATE TRANSITIONS ---------------------------------------------
% Safety-driven only in Pass 2. SOC and balancing logic is Pass 3.
switch state

    case S_INIT
        if      faultA && faultB, state = S_SHUTDOWN;
        elseif  faultA,           state = S_ISOLATE_A;
        elseif  faultB,           state = S_ISOLATE_B;
        else,                     state = S_BOTH;
        end

    case S_BOTH
        if      faultA && faultB, state = S_SHUTDOWN;
        elseif  faultA,           state = S_ISOLATE_A;
        elseif  faultB,           state = S_ISOLATE_B;
        end

    case S_ISOLATE_A
        if faultB, state = S_SHUTDOWN; end   % last healthy string gone

    case S_ISOLATE_B
        if faultA, state = S_SHUTDOWN; end

    case S_SHUTDOWN
        % TERMINAL. No path out. A latched safety trip requires operator
        % reset -- auto-recovery from an unknown fault is not defensible.

    otherwise
        state = S_SHUTDOWN;   % unreachable; fail safe if it ever happens
end

%% ---- OUTPUTS -------------------------------------------------------
switch state
    case S_INIT,       cmd_mainA = 0; cmd_mainB = 0; cmd_load = 0;
    case S_BOTH,       cmd_mainA = 1; cmd_mainB = 1; cmd_load = 1;
    case S_ISOLATE_A,  cmd_mainA = 0; cmd_mainB = 1; cmd_load = 1;
    case S_ISOLATE_B,  cmd_mainA = 1; cmd_mainB = 0; cmd_load = 1;
    case S_SHUTDOWN,   cmd_mainA = 0; cmd_mainB = 0; cmd_load = 0;
    otherwise,         cmd_mainA = 0; cmd_mainB = 0; cmd_load = 0;
end

state_out = state;
fault_out = fault;

% Suppress unused-input warnings. socA/socB arrive now so the interface is
% stable when Pass 3 adds balancing logic.
socA = socA; %#ok<NASGU,ASGSL>
socB = socB; %#ok<NASGU,ASGSL>

end

%% =====================================================================
%  FMEA -- PASS 2
%  =====================================================================
%
%  FAILURE                  DETECTION                  SAFE RESPONSE
%  -----------------------------------------------------------------
%  Cell undervoltage        vA/vB < 2.50 V, 3 samples  Isolate that string;
%                                                      shutdown if both
%
%  Cell overvoltage         vA/vB > 4.20 V, 3 samples  Isolate that string
%
%  Tap sensor open circuit  Reads ~0 V, trips UV       Isolate. Fails safe,
%                                                      though it cannot be
%                                                      distinguished from a
%                                                      genuinely flat cell.
%                                                      [GAP -- see below]
%
%  Tap sensor short to rail Reads high, trips OV       Isolate
%
%  Both strings faulted     faultA && faultB           SHUTDOWN, latched,
%                                                      load disconnected
%
%  MCU reset mid-mission    persistent vars cleared    Re-enters S_INIT with
%                                                      all switches off; the
%                                                      100k pull-downs hold
%                                                      the hardware open
%                                                      through the reset
%
%  Quantisation flicker     Debounce counter resets    No spurious trip
%
%  KNOWN GAPS (Pass 3+):
%    - No overcurrent check       -> needs string current inputs
%    - No overtemperature check   -> needs temperature inputs
%    - No state validation        -> commands are not read back and
%                                    confirmed against measured current.
%                                    A welded-closed MOSFET is currently
%                                    undetectable. REQUIRED before M1.
%    - No cooldown timer          -> topology could oscillate
%    - No dV check before paralleling -> Pass 3