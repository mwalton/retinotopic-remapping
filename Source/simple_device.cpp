/**********************************************************************
  Visual Attention: Retinotopic Spatial Dynamics
  2014 - Michael Walton
**********************************************************************/

/* Notes:
 */

#include "simple_device.h"
#include "Statistics.h"
#include "EPICLib/Geometry.h"
#include "EPICLib/Output_tee_globals.h"
#include "EPICLib/Output_tee.h"
#include "EPICLib/Numeric_utilities.h"
#include "EPICLib/Symbol_utilities.h"
#include "EPICLib/Assert_throw.h"
#include "EPICLib/Device_exception.h"
#include "EPICLib/Standard_symbols.h"

#include <iostream>
#include <iomanip>
#include <string>
#include <sstream>
#include <algorithm>
#include <vector>
//#include <cassert>
#include <cmath>

namespace GU = Geometry_Utilities;
//using GU::Point;
using namespace std;
using std::ostringstream;

// handy internal constants
const Symbol iFix_c("Init_Fixation");
const Symbol sFix_c("Saccade_Fixation");
const Symbol VCue_c("Cue");
const Symbol VProbe_c("Probe");

const Symbol retinotopic_c("Retinotopic");
const Symbol spatiotopic_c("Spatiotopic");
const Symbol intermediate_c("Intermediate");
	
// experiment constants
const GU::Size wstim_size_c(1., 1.);
//const GU::Point vstim_location_c(1., 0.);
const GU::Size vstim_size_c(1., 1.);
const long intertrialinterval_c = 5000;

const bool show_debug = true;
const bool show_states = false;

std::string dataHeader = "\nTASKTYPE,TRIAL,TRIAL_TYPE,PROBE_DELAY,RT,SACCADE_DURATION,ORIENTATION,RESPONSE,CORRECTRESPONSE,ACCURACY,TAG,RULES";

simple_device::simple_device(const std::string& device_name, Output_tee& ot) :
		Device_base(device_name, ot), 
        n_total_trials(0), condition_string("10 8.3 2.5 Draft"), locus_eccentricity(0), cue_proximity(0), n_trials(0), trial(0), vresponse_made(false), tagstr("Draft"),
	state(START) //should this be in initialize? (tls)
{
	// parse condition string and initialize the task
	parse_condition_string();		
	initialize();
}


void simple_device::parse_condition_string()
// modified by ajh to add the second parameter
{
	// build an error message string in case we need it
	string error_msg(condition_string);
	error_msg += "\n Should be: space-delimited trials(int > 0) Locus Eccentricity > 0";
	istringstream iss(condition_string);
	
	int nt;
    double le;
    double cp;
	string ts;
	iss >> nt >> le >> cp >> ts;
	// do all error checks before last read
	if(!iss)
		throw Device_exception(this, string("Incorrect condition string: ") + error_msg);
	if(nt <= 0)
		throw Device_exception(this, string("Number of trials must be positive ") + error_msg);
    if (le < 0)
        throw Device_exception(this, string("Locus width must be positive ") + error_msg);
    if (cp < 0)
        throw Device_exception(this, string("Cue proximity must be positive ") + error_msg);
	
	//assign parameters
	if (tagstr != "") tagstr = ts;
	n_trials = nt;
    locus_eccentricity = le / 2;
    cue_proximity = cp;
}

void simple_device::set_parameter_string(const string& condition_string_)
{
	condition_string = condition_string_;
	parse_condition_string();
}

string simple_device::get_parameter_string() const
{
	return condition_string;
}

void simple_device::initialize()
{
	vresponse_made = false;
	trial = 0;
	state = START;
	current_vrt.reset();
		
    //fill stimulus vector
    vstims.clear();
    vstims.push_back(Blue_c);
    vstims.push_back(Red_c);

    vresps.clear();
    vresps.push_back(Symbol("F"));
    vresps.push_back(Symbol("J"));
	
	//identify the experiment in the device and trace output
    //TODO make parameter output appropriate for task
	if(device_out) {
		device_out << "******************************************" << endl;
		device_out << "Initializeing Device: Retinotopic Attention Task v0.1" << endl;
		device_out << "Conditions: " << condition_string << endl;
		device_out << "******************************************" << endl;
	
		device_out << "**********************************************************************" << endl;
		device_out << " trial start" << endl;
		device_out << " Initial Fixation (500ms)" << endl;
        device_out << " Memory Cue (200ms)" << endl;
        device_out << " Post-cue Fixation (500ms)" << endl;
        device_out << " Initial Fixation (500ms)" << endl;
        device_out << " Saccadic Fixation (250ms)" << endl;
        device_out << " Probe Delay (50-400ms)" << endl;
        
		device_out << " Probe (Red,Green,Blue, or Yellow) circle, " << endl;
		device_out << " {button press --> F or J}" << endl;
		device_out << " Cleanup For Next Trial (ITI = 5000ms)" << endl;
		device_out << "";
		device_out << " Conditions" << endl;
		device_out << " ----------" << endl;
        device_out << " Locus Eccentricity : Controls the distance of attention loci from 0,0 in DVA" << endl;
		device_out << " ----------" << endl;
		device_out << " [note, now a 4th parameter that can be any string, write it to output with data.]" << endl;
		device_out << "**********************************************************************" << endl;
	}
	
	DataOutputString.str("");
	
	// If the streams were open, close.
	// This is just incase the model is re-initialized.
	if(dataoutput_stream.is_open())
	{
		dataoutput_stream.close();
	}	
	
	// open the data output stream for overwriting!
	openOutputFile(dataoutput_stream, string("data_output"));
	reparse_conditionstring = false;
}

// You have to get the ball rolling with a first time-delayed event - nothing happens until you do.
// DK
void simple_device::handle_Start_event()
{
	//	if(device_out)
	//		device_out << processor_info() << "received Start_event" << endl;

	//determine rule file name so that it can be added to data output. 
	prsfilenameonly = "";
	device_out << "@@RuleFile[Full]: " << prsfilename << endl;
	clear_prspathvector();	//clear out prspathvector
	prspathvector = split(prsfilename, '/'); //fill prspathvector with the rule file path
	if (prspathvector.size() > 0) { 
		prsfilenameonly = prspathvector[prspathvector.size() - 1];
	}
	else {
		prsfilenameonly = "?????.prs";
	}
	device_out << "@@RuleFile[NameOnly]: " << prsfilenameonly << endl;
	
	if(device_out) {
		device_out << "******************{{{{{{{{{{{{{{{{__SIMULATION_START__}}}}}}}}}}}}}}}}***************************" << endl;
		device_out << "******************{{{{{{{{{{{{{{{{__SIMULATION_START__}}}}}}}}}}}}}}}}***************************" << endl;
	}
	
 	schedule_delay_event(500);
}

//called after the stop_simulation function (which is bart of the base device class)
void simple_device::handle_Stop_event()
{
	//	if(device_out)
	//		device_out << processor_info() << "received Stop_event" << endl;
	
	//show final stats. 	
	output_statistics();
	
	refresh_experiment();
	
	//close data output file
	if(dataoutput_stream.is_open())
	{
		dataoutput_stream.close();
	}		
}

// STATES: {START, START_TRIAL, PRESENT_CUE, REMOVE_CUE, REMOVE_FIXATION, WAIT_FOR_FIXATION, PRESENT_PROBE, WAITING_FOR_RESPONSE, DISCARD_PROBE, SHUTDOWN}

void simple_device::handle_Delay_event(const Symbol& type, const Symbol& datum, 
		const Symbol& object_name, const Symbol& property_name, const Symbol& property_value)
{	
	switch(state) {
		case START:
			if (show_states) show_message("********-->STATE: START",true);
			state = START_TRIAL;
			schedule_delay_event(500);
			break;
		case START_TRIAL:
			if (show_states) show_message("********-->STATE: START_TRIAL",true);
			vresponse_made = false;
			start_trial();
			state = PRESENT_CUE;
			schedule_delay_event(500);
			break;
        case PRESENT_CUE:
            if (show_states) show_message("********-->STATE: PRESENT_CUE",true);
            present_cue();
            state = REMOVE_CUE;
            schedule_delay_event(200);
            break;
		case REMOVE_CUE:
			if (show_states) show_message("********-->STATE: REMOVE_CUE",true);
			remove_cue();
			state = REMOVE_FIXATION;
			schedule_delay_event(500);
			break;
		case REMOVE_FIXATION:
			if (show_states) show_message("********-->STATE: REMOVE_FIXATION",true);
			remove_fixation();
            present_saccade_target();
			state = WAITFOR_EYEMOVE;
			break;
        case WAITFOR_EYEMOVE:
            if (show_states) show_message("********-->STATE: WAITFOR_EYEMOVE",true);
			//nothing to do, just wait
			//**note: next state and schedule_delay set by response handler
			break;
        case PRESENT_PROBE:
            if (show_states) show_message("********-->STATE: PRESENT_PROBE",true);
            remove_saccade_target();
            present_probe();
            state = WAITING_FOR_RESPONSE;
            break;
		case WAITING_FOR_RESPONSE:
			if (show_states) show_message("********-->STATE: WAITING_FOR_RESPONSE",true);
			//nothing to do, just wait
			//**note: next state and schedule_delay set by response handler
			break;
		case DISCARD_PROBE:
			if (show_states) show_message("********-->STATE: REMOVING_PROBE",true);
			//remove the stimulus
			remove_probe();
			break;
		case SHUTDOWN:
			if (show_states) show_message("********-->STATE: SHUTDOWN",true);
			//Detected Signal to Stop Simulation.
			stop_simulation();
			break;
		default:
			throw Device_exception(this, "Device delay event in unknown or improper device state");
			break;
		}
}

//At Trial Start, Warning Stimuli are presented
void simple_device::start_trial()
{
	if (show_debug) show_message("*trial_start|");
	
	//only occurs if task is stopped and restarted
	if (reparse_conditionstring == true) {
		parse_condition_string(); 
		initialize();
	}
	
	trial++; //increment trial counter
	
	present_fixation();
	
	if (show_debug) show_message("trial_start*", true);
}

void simple_device::present_fixation() {
    if (show_debug) show_message("*present_fixation|");
    
    int select_region = rand() % 4;
    double fix_x;
    double fix_y;
    
    switch (select_region) {
        case 0:
            fix_x = -1 * locus_eccentricity;
            fix_y = -1 * locus_eccentricity;
            break;
        case 1:
            fix_x = locus_eccentricity;
            fix_y = -1 * locus_eccentricity;
            break;
        case 2:
            fix_x = -1 * locus_eccentricity;
            fix_y = locus_eccentricity;
            break;
        default:
            fix_x = locus_eccentricity;
            fix_y = locus_eccentricity;
            break;
    }
    
    init_fix_name = concatenate_to_Symbol(iFix_c, trial);
    init_fix_location = GU::Point(fix_x,fix_y);
    
    make_visual_object_appear(init_fix_name, init_fix_location, wstim_size_c);
	set_visual_object_property(init_fix_name, Shape_c, Empty_Circle_c);
    set_visual_object_property(init_fix_name, Color_c, Gray_c);
    
	//vstim_onset = get_time();  //MOVE ME
    
    if (show_debug) show_message("present_fixation*", true);
}

void simple_device::present_cue()
{
	
	if (show_debug) show_message("*present_cue|");
    
    int select_region = rand() % 4;
    double cue_x;
    double cue_y;
    
    switch (select_region) {
        case 0:
            cue_x = -1 * cue_proximity + init_fix_location.x;
            cue_y = -1 * cue_proximity + init_fix_location.y;
            break;
        case 1:
            cue_x = cue_proximity + init_fix_location.x;
            cue_y = -1 * cue_proximity + init_fix_location.y;
            break;
        case 2:
            cue_x = -1 * cue_proximity + init_fix_location.x;
            cue_y = cue_proximity + init_fix_location.y;
            break;
        default:
            cue_x = cue_proximity + init_fix_location.x;
            cue_y = cue_proximity + init_fix_location.y;
            break;
    }
    
    cue_location = GU::Point(cue_x, cue_y);
	
	//display visual fixation piont 
	cue_name = concatenate_to_Symbol(VCue_c, trial);
	make_visual_object_appear(cue_name, cue_location, wstim_size_c);
	set_visual_object_property(cue_name, Shape_c, Empty_Square_c);
    set_visual_object_property(cue_name, Color_c, Black_c);
	
	if (show_debug) show_message("present_cue*", true);
}

void simple_device::remove_cue()
{
	if (show_debug) show_message("*remove_cue|");
	
	// remove the warningstimulus
	make_visual_object_disappear(cue_name);
	
	if (show_debug) show_message("remove_cue*", true);
}

void simple_device::remove_fixation()
{
	if (show_debug) show_message("*removing_fixation|");
	
	// remove the stimulus
	make_visual_object_disappear(init_fix_name);
	
	setup_next_trial();
	if (show_debug) show_message("....removing_fixation*");
}

void simple_device::present_saccade_target() {
    if (show_debug) show_message("*present_saccade_fixation|");
    
    int select_region = rand() % 4;
    double fix_x;
    double fix_y;
    
    switch (select_region) {
        case 0:
            fix_x = -1 * locus_eccentricity;
            fix_y = -1 * locus_eccentricity;
            break;
        case 1:
            fix_x = locus_eccentricity;
            fix_y = -1 * locus_eccentricity;
            break;
        case 2:
            fix_x = -1 * locus_eccentricity;
            fix_y = locus_eccentricity;
            break;
        default:
            fix_x = locus_eccentricity;
            fix_y = locus_eccentricity;
            break;
    }
    
    //prevent choosing a saccade target that is the same location as fixation
    if (fix_x == init_fix_location.x && fix_y == init_fix_location.y) {
        fix_x = fix_x * -1;
    }
    //only allow saccade targets that are adjacent to the initial fixation
    if (fix_x == -1 * init_fix_location.x && fix_y == -1 * init_fix_location.y) {
        fix_y = fix_y * -1;
    }
    
    sacc_fix_name = concatenate_to_Symbol(sFix_c, trial);
    sacc_fix_location = GU::Point(fix_x,fix_y);
    
    make_visual_object_appear(sacc_fix_name, sacc_fix_location, wstim_size_c);
	set_visual_object_property(sacc_fix_name, Shape_c, Empty_Circle_c);
    set_visual_object_property(sacc_fix_name, Color_c, Gray_c);
    
    starget_onset = get_time();

    
    if (show_debug) show_message("present_saccade_fixation*", true);
}

void simple_device::handle_Eyemovement_End_event(const Symbol& target_name, GU::Point new_location) {
    if (show_debug) show_message("*handle_Eyemovement_End_event....",true);
    
    if (state == WAITFOR_EYEMOVE && new_location == sacc_fix_location) {
        
        state = PRESENT_PROBE;
        saccade_duration = get_time() - starget_onset;
        
        int select_delay = rand() % 3;
        
        switch (select_delay) {
            case 0:
                probe_delay = 50;
                break;
            case 1:
                probe_delay = 250;
                break;
            default:
                probe_delay = 400;
                break;
        }
        
        schedule_delay_event(probe_delay);
    }
}

void simple_device::present_probe()
{

	//show the stimulus
	make_vis_stim_appear();
}

void simple_device::make_vis_stim_appear()
{
    double probe_x;
    double probe_y;
    
	if (show_debug) show_message("*make_vis_stim_appear|");
	int stim_index = rand() % 2;				// chooses one of the vstims to display
    
	vstim_color = vstims.at(stim_index);
    probe_orientation = (stim_index == 0) ? -45 : 45;
	correct_vresp = (stim_index == 0) ? vresps.at(0) : vresps.at(1); //fixme: response mapping
	vstim_name = concatenate_to_Symbol(VProbe_c, trial);
    
    int select_trial_type = rand() % 3;
    //select_trial_type = 2;
    switch (select_trial_type) {
        case 0:
            probe_location = cue_location;
            trial_type = spatiotopic_c;
            break;
        case 1:
            probe_x = sacc_fix_location.x + (cue_location.x - init_fix_location.x);
            probe_y = sacc_fix_location.y + (cue_location.y - init_fix_location.y);
            
            probe_location = GU::Point(probe_x, probe_y);
            trial_type = retinotopic_c;
            break;
        default:
            probe_x = ((sacc_fix_location.x - init_fix_location.x) / 2)  + cue_location.x;
            probe_y = ((sacc_fix_location.y - init_fix_location.y) / 2) + cue_location.y;
            
            probe_location = GU::Point(probe_x, probe_y);
            trial_type = intermediate_c;
            break;
    }
	
	make_visual_object_appear(vstim_name, probe_location, vstim_size_c);
	set_visual_object_property(vstim_name, Shape_c, Line_c);
    set_visual_object_property(vstim_name, Color_c, vstim_color);
    set_visual_object_property(vstim_name, Orientation_c, probe_orientation);
	 
	vstim_onset = get_time();
	vresponse_made = false;
	if (show_debug) show_message("make_vis_stim_appear*", true);
}


// here if a keystroke event is received
void simple_device::handle_Keystroke_event(const Symbol& key_name)
{
	if (show_debug) show_message("*handle_Keystroke_event....",true);
	//ostringstream outputString;  //defined in simple_device.h (tls)
	outputString.str("");
    std::string isCorrect;
    long rt = get_time() - vstim_onset;
	
	if(key_name == correct_vresp) {
        isCorrect = "CORRECT";
		if(trial > 1) current_vrt.update(rt);
	}
	else {
        isCorrect = "INCORRECT";
		//throw Device_exception(this, string("Unrecognized keystroke: ") + key_name.str());
		//if(trial > 1) current_vrt.update(rt); //don't average incorrect responses
	}
    
    outputString
    << "Trial # " << trial
    << " | (retinotopictask) | RT: " << rt
    << " | Trial Type: " << trial_type
    << " | Initial Fixation: (" << init_fix_location.x << "," << init_fix_location.y << ")"
    << " | Cue Location: (" << cue_location.x << "," << cue_location.y << ")"
    << " | Saccade Target: (" << sacc_fix_location.x << "," << sacc_fix_location.y << ")"
    << " | Saccade Duration: " << saccade_duration
    << " | Probe Location: (" << probe_location.x << "," << probe_location.y << ")"
    << " | Probe Delay: " << probe_delay
    << " | Probe Orientation: " << probe_orientation
    << " | Keystroke: " << key_name
    << " | CorrectResponse: " << correct_vresp
    << " | (" << isCorrect << ")" << endl;
    
    DataOutputString << "RETINOTOPICTASK" << ","
    << trial << ","
    << trial_type << ","
    << probe_delay << ","
    << rt << ","
    << saccade_duration << ","
    << probe_orientation << ","
    << key_name << ","
    << correct_vresp << ","
    << isCorrect << ","
    << tagstr << ","
    << prsfilenameonly << endl;
    
	show_message(outputString.str());
	vresponse_made = true;
	
	if (show_debug) show_message("....handle_Keystroke_event*");
	
	state = DISCARD_PROBE;
	schedule_delay_event(500);
}

void simple_device::remove_probe()
{
	if (show_debug) show_message("*removing_probe|");
	
	// remove the stimulus
	make_visual_object_disappear(vstim_name);
	
	setup_next_trial();
	if (show_debug) show_message("....removing_probe*");
}

void simple_device::remove_saccade_target() {
    if (show_debug) show_message("*removing_saccade_target|");
    make_visual_object_disappear(sacc_fix_name);
    if (show_debug) show_message("....removing_saccade_target*");
}

void simple_device::setup_next_trial()
{	
	// set up another trial if the experiment is to continue
	if(trial < n_trials) {
		if (show_debug) show_message("*setup_next_trial|");
		state = START_TRIAL;
		schedule_delay_event(intertrialinterval_c);
		if (show_debug) show_message("setup_next_trial*");
	}
	else { 
		if (show_debug) show_message("*shutdown_experiment|");
		state = SHUTDOWN;
		schedule_delay_event(500);
		//stop_simulation();
		if (show_debug) show_message("shutdown_experiment*");
	}
}

void simple_device::refresh_experiment() {
	// call this from very last procedure...currently that is output_statistics()
	
	vresponse_made = false;
	trial = 0;
	state = START;
	current_vrt.reset();
	reparse_conditionstring = true;
}

void simple_device::output_statistics() //const
{
	if (show_debug) show_message("*output_statistics|");
	
	show_message("*** End of experiment! ***",true);

	// show condition
	outputString.str("");
	outputString << "\nCONDITION = ";
	
	show_message(outputString.str());	
	
	// show total trials
	outputString.str("");
	outputString << "\nTotal trials = " << '\t' << n_trials << endl;
	show_message(outputString.str());

	// show performance
	outputString.str("");
	outputString << "N = " << setw(3) << current_vrt.get_n() 
			<< ", RT = " << fixed << setprecision(0) << setw(4) << current_vrt.get_mean();
	show_message(outputString.str(),true);
	show_message(" ", true);
	show_message("NOTE: Average Ignores 1st Trial",true);
					
	show_message("*** ****************** ***",true);
	
	refresh_experiment();

	if (show_debug) show_message("output_statistics*",true);
	
	show_message("************* RAW DATA ***************");
	outputString.str("");
	outputString << dataHeader << endl;
	show_message(outputString.str());
	show_message(DataOutputString.str());
	show_message("**************************************");
	
	//Write Collated Raw Data To Output File
	dataoutput_stream << DataOutputString.str();	
}

void simple_device::show_message(const std::string& thestring, const bool addendl) {

	if (get_trace() && Trace_out) Trace_out << thestring;
	if (device_out) device_out << thestring;

	if (addendl) {
		if (get_trace() && Trace_out) Trace_out << endl;
		if (device_out) device_out << endl;
	}
}

void simple_device::openOutputFile(ofstream & outFileStream, const string filename_text)

{
	string fileName = filename_text + ".csv";
	bool filealreadyexists = fexists(fileName.c_str());
	//outFileStream.open((fileName).c_str(), ofstream::out); //overwriting
	outFileStream.open((fileName).c_str(), ofstream::app); //appending
	if(!outFileStream.is_open()) {
		throw Device_exception(this, " Error opening output file: " + fileName);
		show_message("Error opening output file:" + fileName, true);
	} 
	else if (!filealreadyexists) 
		outFileStream << dataHeader << endl;
	
}

bool simple_device::fexists(const char *filename)
{
	ifstream ifile(filename);
	return ifile;
}

//------------------------------------------------------------------------------
// Split string by a delim. The first puts the results in an already constructed 
// vector, the second returns a new vector. Note that this solution does not skip 
// empty tokens, so the following will find 4 items, one of which is empty:
// std::vector<std::string> x = split("one:two::three", ':');
//------------------------------------------------------------------------------
std::vector<std::string> &simple_device::split(const std::string &s, char delim, std::vector<std::string> &elems) {
	std::stringstream ss(s);
	std::string item;
	while(std::getline(ss, item, delim)) {
		elems.push_back(item);
	}
	return elems;
} 

std::vector<std::string> simple_device::split(const std::string &s, char delim) {
	std::vector<std::string> elems;     
	return split(s, delim, elems); 
}


void simple_device::clear_prspathvector() {  
	vector<std::string>().swap(prspathvector);  
}
