#include "stdafx.h"
#include "ModeS2.h"


const string updateUrl = "";
const int VERSION_CODE = 10;

vector<string>	EQUIPEMENT_CODES = { "H", "L", "E", "G", "W", "Q", "S" };
vector<string>	ICAO_MODES = { "EB", "EL", "LS", "ET", "ED", "LF", "EH", "LK", "LO", "LIM, LIR" };

HttpHelper *httpHelper = NULL;

std::vector<std::string> &split(const std::string &s, char delim, std::vector<std::string> &elems) {
	std::stringstream ss(s);
	std::string item;
	while (std::getline(ss, item, delim)) {
		elems.push_back(item);
	}
	return elems;
}

std::vector<std::string> split(const std::string &s, char delim) {
	std::vector<std::string> elems;
	split(s, delim, elems);
	return elems;
}

void doInitialLoad(void * arg)
{
	string message;
	message.assign(httpHelper->downloadStringFromURL(updateUrl));

	// Message format is {equip_codes}|{icao_modes}|{version}
	if (regex_match(message, std::regex("^([A-z,]+)[|]([A-z,]+)[|]([0-9]{1,2})$")))
	{
		vector<string> data = split(message, '|');

		EQUIPEMENT_CODES = split(data.front(), ',');
		ICAO_MODES = split(data.at(1), ',');

		if (std::stoi(data.back(), nullptr, 0) > VERSION_CODE)
		{
			AFX_MANAGE_STATE(AfxGetStaticModuleState());
				AfxMessageBox("A new version of the mode S plugin is available, please update it.");
		}
	}
}


CModeS::CModeS(void):CPlugIn(EuroScopePlugIn::COMPATIBILITY_CODE,
	"Mode S PlugIn",
	"1.0.0e32",
	"Pierre Ferran",
	"GPL v3")
{
	if (httpHelper == NULL)
		httpHelper = new HttpHelper();

	RegisterTagItemType("Transponder type", TAG_ITEM_ISMODES);
	RegisterTagItemType("Mode S: Reported Heading", TAG_ITEM_MODESHDG); 
	RegisterTagItemType("Mode S: Roll Angle", TAG_ITEM_MODESROLLAGL);
	RegisterTagItemType("Mode S: Reported GS", TAG_ITEM_MODESREPGS);

	RegisterTagItemFunction("Assign mode S squawk", TAG_FUNC_ASSIGNMODES);

	DisplayUserMessage("Message", "Info", "Downloading mode S configuration...", true, false, false, false, false);
	// Download the configuration
	_beginthread(doInitialLoad, 0, NULL);
	DisplayUserMessage("Message", "Info", "Downloading mode S configuration...", true, false, false, false, false);
}

CModeS::~CModeS(void)
{
	delete httpHelper;
}

void CModeS::OnGetTagItem(CFlightPlan FlightPlan, CRadarTarget RadarTarget, int ItemCode, int TagData, char sItemString[16], int * pColorCode, COLORREF * pRGB, double * pFontSize)
{

	if (ItemCode == TAG_ITEM_ISMODES) {
		if (!FlightPlan.IsValid())
			return;

		if (isAcModeS(FlightPlan)) {
			strcpy_s(sItemString, 16, "S");
		}
		else {
			strcpy_s(sItemString, 16, "A");
		}
	}

	if (ItemCode == TAG_ITEM_MODESHDG)
	{
		if (!FlightPlan.IsValid() || !RadarTarget.IsValid())
			return;

		if (isAcModeS(FlightPlan))
		{
			string rhdg = padWithZeros(3, RadarTarget.GetPosition().GetReportedHeading());
			strcpy_s(sItemString, 16, rhdg.c_str());
		}
	}

	if (ItemCode == TAG_ITEM_MODESROLLAGL)
	{
		if (!FlightPlan.IsValid() || !RadarTarget.IsValid())
			return;

		if (isAcModeS(FlightPlan))
		{
			int rollb = RadarTarget.GetPosition().GetReportedBank();
			string roll = "L";
			if (rollb < 0)
			{
				roll = "R";
			}
			roll += std::to_string(abs(rollb));

			strcpy_s(sItemString, 16, roll.c_str());
		}
	}

	if (ItemCode == TAG_ITEM_MODESREPGS)
	{
		if (!FlightPlan.IsValid() || !RadarTarget.IsValid())
			return;

		if (isAcModeS(FlightPlan) && FlightPlan.GetCorrelatedRadarTarget().IsValid())
		{
			strcpy_s(sItemString, 16, std::to_string(RadarTarget.GetPosition().GetReportedGS()).c_str());
		}

	}

}

void CModeS::OnFunctionCall(int FunctionId, const char * sItemString, POINT Pt, RECT Area)
{
	CFlightPlan FlightPlan = FlightPlanSelectASEL();

	if (!FlightPlan.IsValid())
		return;

	if (FunctionId == TAG_FUNC_ASSIGNMODES && isAcModeS(FlightPlan)) {
		string Dest = FlightPlan.GetFlightPlanData().GetDestination();

		for (auto &zone : ICAO_MODES) // access by reference to avoid copying
		{
			if (zone == Dest) {
				FlightPlan.GetControllerAssignedData().SetSquawk(mode_s_code);
				break;
			}
		}
	}

}

void CModeS::OnFlightPlanFlightPlanDataUpdate(CFlightPlan FlightPlan)
{
	if (!FlightPlan.IsValid())
		return;

	// Here we assign squawk 1000 to ground aircrafts

	const char * assr = FlightPlan.GetControllerAssignedData().GetSquawk();

	if ((strlen(assr) == 0 ||
		strcmp(assr, "0000") == 0 ||
		strcmp(assr, "2000") == 0 ||
		strcmp(assr, "1200") == 0 ||
		strcmp(assr, "2200") == 0) && RadarTargetSelect(FlightPlan.GetCallsign()).GetGS() < 20)
	{
		if (isAcModeS(FlightPlan))
		{
			bool isDestModeS = false;

			for (auto &zone : ICAO_MODES)
			{
				if (zone == string(FlightPlan.GetFlightPlanData().GetDestination())) {
					isDestModeS = true;
					break;
				}
			}

			for (auto &zone : ICAO_MODES)
			{
				if (!isDestModeS)
					break;

				if (zone == string(FlightPlan.GetFlightPlanData().GetOrigin())) {
						FlightPlan.GetControllerAssignedData().SetSquawk(mode_s_code);
						break;
				}
			}
		}
	}
}

bool CModeS::isAcModeS(CFlightPlan FlightPlan) {
	stringstream ss;
	string transponder_type;
	char mychar = FlightPlan.GetFlightPlanData().GetCapibilities();
	ss << mychar;
	ss >> transponder_type;

	for (auto &code : EQUIPEMENT_CODES) {
		if (transponder_type == code)
		{
			return true;
		}
	}
	return false;
}