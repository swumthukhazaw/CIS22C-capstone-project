#include <crow_all.h>   // Make sure crow_all.h is in your include/ folder
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <unordered_map>
#include <map>
#include <algorithm>
#include <string>
#include <cstdlib>
#include <cctype>
#include <utility>
#include <cmath>

// ----------------- Data structures -----------------

struct Airline {
    int id;                 // OpenFlights airline ID
    std::string iata;       // IATA code (may be empty)
    std::string name;
    std::string country;
    bool active;            // from OpenFlights "Y/N"
};

struct Airport {
    int id;                 // OpenFlights airport ID
    std::string iata;       // IATA code (may be empty)
    std::string name;
    std::string city;
    std::string country;
    double latitude;
    double longitude;
};

struct Route {
    int airlineId;          // Airline ID (OpenFlights)
    int srcAirportId;       // Source airport ID
    int dstAirportId;       // Destination airport ID
    int stops;              // Number of stops
};

// Global containers
std::vector<Airline> airlines;
std::vector<Airport> airports;
std::vector<Route>   routes;

// Lookups (by ID and by IATA)
std::unordered_map<int, std::size_t> airlineIdToIndex;
std::unordered_map<std::string, std::size_t> airlineIataToIndex;

std::unordered_map<int, std::size_t> airportIdToIndex;
std::unordered_map<std::string, std::size_t> airportIataToIndex;

// Adjacency: routes from a source airport (by source airport ID)
std::unordered_map<int, std::vector<std::size_t> > routesFromSrc;

// ----------------- Utility helpers -----------------

static std::string trim(const std::string& s)
{
    std::size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) {
        ++start;
    }
    std::size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
        --end;
    }
    return s.substr(start, end - start);
}

static std::string toUpper(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(s[i]))));
    }
    return out;
}

// Simple CSV splitter that understands quotes (no escaped quotes).
static std::vector<std::string> splitCSVLine(const std::string& line)
{
    std::vector<std::string> result;
    std::string cur;
    bool inQuotes = false;

    for (std::size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (c == '"') {
            inQuotes = !inQuotes;
        } else if (c == ',' && !inQuotes) {
            result.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    result.push_back(cur);

    return result;
}

// ----------------- Loaders -----------------

// Assumes standard OpenFlights airlines.dat format:
// 0:Airline ID,1:Name,2:Alias,3:IATA,4:ICAO,5:Callsign,6:Country,7:Active
bool loadAirlines(const std::string& filename)
{
    std::ifstream in(filename.c_str());
    if (!in) {
        std::cerr << "Failed to open airlines file: " << filename << std::endl;
        return false;
    }

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        std::vector<std::string> fields = splitCSVLine(line);
        if (fields.size() < 8) continue;

        Airline a;
        // ID
        if (fields[0] == "\\N" || fields[0].empty()) continue;
        a.id = std::atoi(fields[0].c_str());

        a.name = trim(fields[1]);
        a.iata = toUpper(trim(fields[3]));
        a.country = trim(fields[6]);
        std::string active = trim(fields[7]);
        a.active = (active == "Y" || active == "y" || active == "1");

        std::size_t index = airlines.size();
        airlines.push_back(a);

        airlineIdToIndex[a.id] = index;
        if (!a.iata.empty() && a.iata != "\\N") {
            airlineIataToIndex[a.iata] = index;
        }
    }
    std::cerr << "Loaded " << airlines.size() << " airlines" << std::endl;
    return true;
}

// Assumes standard OpenFlights airports.dat format:
// 0:Airport ID,1:Name,2:City,3:Country,4:IATA,5:ICAO,6:Latitude,7:Longitude,...
bool loadAirports(const std::string& filename)
{
    std::ifstream in(filename.c_str());
    if (!in) {
        std::cerr << "Failed to open airports file: " << filename << std::endl;
        return false;
    }

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        std::vector<std::string> fields = splitCSVLine(line);
        if (fields.size() < 8) continue;

        Airport a;
        if (fields[0] == "\\N" || fields[0].empty()) continue;
        a.id = std::atoi(fields[0].c_str());

        a.name = trim(fields[1]);
        a.city = trim(fields[2]);
        a.country = trim(fields[3]);
        a.iata = toUpper(trim(fields[4]));

        a.latitude = std::atof(fields[6].c_str());
        a.longitude = std::atof(fields[7].c_str());

        std::size_t index = airports.size();
        airports.push_back(a);

        airportIdToIndex[a.id] = index;
        if (!a.iata.empty() && a.iata != "\\N") {
            airportIataToIndex[a.iata] = index;
        }
    }
    std::cerr << "Loaded " << airports.size() << " airports" << std::endl;
    return true;
}

// Assumes standard OpenFlights routes.dat format:
// 0:Airline,1:Airline ID,2:Source airport,3:Source airport ID,
// 4:Destination airport,5:Destination airport ID,6:Codeshare,
// 7:Stops,8:Equipment
bool loadRoutes(const std::string& filename)
{
    std::ifstream in(filename.c_str());
    if (!in) {
        std::cerr << "Failed to open routes file: " << filename << std::endl;
        return false;
    }

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        std::vector<std::string> fields = splitCSVLine(line);
        if (fields.size() < 9) continue;

        // Require airline ID, source ID, dest ID
        if (fields[1] == "\\N" || fields[3] == "\\N" || fields[5] == "\\N") {
            continue;
        }

        Route r;
        r.airlineId = std::atoi(fields[1].c_str());
        r.srcAirportId = std::atoi(fields[3].c_str());
        r.dstAirportId = std::atoi(fields[5].c_str());
        r.stops = std::atoi(fields[7].c_str());

        std::size_t index = routes.size();
        routes.push_back(r);

        // Build adjacency
        routesFromSrc[r.srcAirportId].push_back(index);
    }

    std::cerr << "Loaded " << routes.size() << " routes" << std::endl;
    return true;
}

// ----------------- Lookup helpers -----------------

const Airline* findAirlineByIata(const std::string& iataRaw)
{
    std::string iata = toUpper(iataRaw);
    std::unordered_map<std::string, std::size_t>::const_iterator it =
        airlineIataToIndex.find(iata);
    if (it == airlineIataToIndex.end()) return NULL;
    return &airlines[it->second];
}

const Airport* findAirportByIata(const std::string& iataRaw)
{
    std::string iata = toUpper(iataRaw);
    std::unordered_map<std::string, std::size_t>::const_iterator it =
        airportIataToIndex.find(iata);
    if (it == airportIataToIndex.end()) return NULL;
    return &airports[it->second];
}

const Airport* findAirportById(int id)
{
    std::unordered_map<int, std::size_t>::const_iterator it = airportIdToIndex.find(id);
    if (it == airportIdToIndex.end()) return NULL;
    return &airports[it->second];
}

const Airline* findAirlineById(int id)
{
    std::unordered_map<int, std::size_t>::const_iterator it = airlineIdToIndex.find(id);
    if (it == airlineIdToIndex.end()) return NULL;
    return &airlines[it->second];
}

// ----------------- Report helpers -----------------

// For a given airline ID, return map: airportId -> routeCount (both directions)
std::map<int, int> countRoutesByAirline(int airlineId)
{
    std::map<int, int> counts;
    for (std::size_t i = 0; i < routes.size(); ++i) {
        const Route& r = routes[i];
        if (r.airlineId != airlineId) continue;

        // Count both ends as "served"
        counts[r.srcAirportId] += 1;
        counts[r.dstAirportId] += 1;
    }
    return counts;
}

// For a given airport ID, return map: airlineId -> routeCount (routes touching this airport)
std::map<int, int> countRoutesByAirport(int airportId)
{
    std::map<int, int> counts;
    for (std::size_t i = 0; i < routes.size(); ++i) {
        const Route& r = routes[i];
        if (r.srcAirportId == airportId || r.dstAirportId == airportId) {
            counts[r.airlineId] += 1;
        }
    }
    return counts;
}

// ----------------- Distance helpers for one-hop -----------------

static double deg2rad(double deg) {
    const double PI = 3.14159265358979323846;
    return deg * PI / 180.0;
}


// Great-circle distance in *air miles* between two airports
double greatCircleMiles(const Airport& a, const Airport& b)
{
    // Haversine
    const double R_km = 6371.0;
    double lat1 = deg2rad(a.latitude);
    double lon1 = deg2rad(a.longitude);
    double lat2 = deg2rad(b.latitude);
    double lon2 = deg2rad(b.longitude);

    double dlat = lat2 - lat1;
    double dlon = lon2 - lon1;
    double sin_dlat = std::sin(dlat / 2.0);
    double sin_dlon = std::sin(dlon / 2.0);

    double h = sin_dlat * sin_dlat +
               std::cos(lat1) * std::cos(lat2) * sin_dlon * sin_dlon;

    double c = 2.0 * std::atan2(std::sqrt(h), std::sqrt(1.0 - h));
    double dist_km = R_km * c;
    return dist_km * 0.621371; // km -> miles
}

// ----------------- Crow handlers -----------------

int main(int argc, char* argv[])
{
    // Adjust file paths as needed
    std::string airlinesFile = "airlines.dat";
    std::string airportsFile = "airports.dat";
    std::string routesFile   = "routes.dat";

    if (argc >= 4) {
        airlinesFile = argv[1];
        airportsFile = argv[2];
        routesFile   = argv[3];
    }

    if (!loadAirlines(airlinesFile) ||
        !loadAirports(airportsFile) ||
        !loadRoutes(routesFile)) {
        std::cerr << "Error loading data files. Exiting." << std::endl;
        return 1;
    }

    crow::SimpleApp app;

    // Serve index.html at root
    CROW_ROUTE(app, "/")
    ([](){
        std::ifstream file("static/index.html");
        if (!file) {
            return crow::response(500, "index.html not found in /static");
        }
        std::stringstream buffer;
        buffer << file.rdbuf();
        crow::response resp(buffer.str());
        resp.set_header("Content-Type", "text/html; charset=UTF-8");
        return resp;
    });

    // Serve static files from /static folder
    CROW_ROUTE(app, "/static/<string>")
    ([](const std::string& filename){
        std::ifstream file("static/" + filename);
        if (!file) {
            return crow::response(404);
        }
        std::stringstream buffer;
        buffer << file.rdbuf();
        return crow::response(buffer.str());
    });

    // /id endpoint
    CROW_ROUTE(app, "/id")([](){
        crow::json::wvalue x;
        x["name"] = "Swum Thukha Zaw";      // <-- your name
        x["deanza_id"] = "20603503";       // <-- your De Anza ID
        return crow::response{x};
    });

    // Given Airline IATA, return full airline record
    CROW_ROUTE(app, "/airline").methods(crow::HTTPMethod::Get)
    ([](const crow::request& req){
        const char* param = req.url_params.get("iata");
        if (!param) {
            return crow::response(400, "Missing 'iata' query parameter");
        }
        std::string iata = param;
        const Airline* a = findAirlineByIata(iata);
        if (!a) {
            return crow::response(404, "Airline not found");
        }

        crow::json::wvalue j;
        j["id"] = a->id;
        j["iata"] = a->iata;
        j["name"] = a->name;
        j["country"] = a->country;
        j["active"] = a->active;
        return crow::response{j};
    });

    // Given Airport IATA, return full airport record
    CROW_ROUTE(app, "/airport").methods(crow::HTTPMethod::Get)
    ([](const crow::request& req){
        const char* param = req.url_params.get("iata");
        if (!param) {
            return crow::response(400, "Missing 'iata' query parameter");
        }
        std::string iata = param;
        const Airport* a = findAirportByIata(iata);
        if (!a) {
            return crow::response(404, "Airport not found");
        }

        crow::json::wvalue j;
        j["id"] = a->id;
        j["iata"] = a->iata;
        j["name"] = a->name;
        j["city"] = a->city;
        j["country"] = a->country;
        j["latitude"] = a->latitude;
        j["longitude"] = a->longitude;
        return crow::response{j};
    });

    // Given Airline IATA, return airline name and list of airports it serves with #routes
    CROW_ROUTE(app, "/airline-routes").methods(crow::HTTPMethod::Get)
    ([](const crow::request& req){
        const char* param = req.url_params.get("iata");
        if (!param) {
            return crow::response(400, "Missing 'iata' query parameter");
        }
        std::string iata = param;
        const Airline* airline = findAirlineByIata(iata);
        if (!airline) {
            return crow::response(404, "Airline not found");
        }

        std::map<int, int> counts = countRoutesByAirline(airline->id);

        struct AirportCount {
            int airportId;
            int count;
        };
        std::vector<AirportCount> v;
        for (std::map<int,int>::const_iterator it = counts.begin();
             it != counts.end(); ++it) {
            AirportCount ac;
            ac.airportId = it->first;
            ac.count = it->second;
            v.push_back(ac);
        }
        std::sort(v.begin(), v.end(),
            [](const AirportCount& a, const AirportCount& b){
                return a.count > b.count;
            });

        crow::json::wvalue j;
        j["airline"]["id"] = airline->id;
        j["airline"]["iata"] = airline->iata;
        j["airline"]["name"] = airline->name;

        j["airports"] = crow::json::wvalue::list();
        auto& arr = j["airports"];

        for (std::size_t i = 0; i < v.size(); ++i) {
            int airportId = v[i].airportId;
            int count = v[i].count;
            std::unordered_map<int,std::size_t>::const_iterator itIndex =
                airportIdToIndex.find(airportId);
            if (itIndex == airportIdToIndex.end()) continue;
            const Airport& ap = airports[itIndex->second];

            crow::json::wvalue item;
            item["airport_id"] = ap.id;
            item["iata"] = ap.iata;
            item["name"] = ap.name;
            item["city"] = ap.city;
            item["country"] = ap.country;
            item["route_count"] = count;

            arr[i] = std::move(item);
        }

        return crow::response{j};
    });

    // Given Airport IATA, return airport name and list of airlines that fly there with #routes
    CROW_ROUTE(app, "/airport-routes").methods(crow::HTTPMethod::Get)
    ([](const crow::request& req){
        const char* param = req.url_params.get("iata");
        if (!param) {
            return crow::response(400, "Missing 'iata' query parameter");
        }
        std::string iata = param;
        const Airport* airport = findAirportByIata(iata);
        if (!airport) {
            return crow::response(404, "Airport not found");
        }

        std::map<int, int> counts = countRoutesByAirport(airport->id);

        struct AirlineCount {
            int airlineId;
            int count;
        };
        std::vector<AirlineCount> v;
        for (std::map<int,int>::const_iterator it = counts.begin();
             it != counts.end(); ++it) {
            AirlineCount ac;
            ac.airlineId = it->first;
            ac.count = it->second;
            v.push_back(ac);
        }
        std::sort(v.begin(), v.end(),
            [](const AirlineCount& a, const AirlineCount& b){
                return a.count > b.count;
            });

        crow::json::wvalue j;
        j["airport"]["id"] = airport->id;
        j["airport"]["iata"] = airport->iata;
        j["airport"]["name"] = airport->name;
        j["airport"]["city"] = airport->city;
        j["airport"]["country"] = airport->country;

        j["airlines"] = crow::json::wvalue::list();
        auto& arr = j["airlines"];

        for (std::size_t i = 0; i < v.size(); ++i) {
            int airlineId = v[i].airlineId;
            int count = v[i].count;
            std::unordered_map<int,std::size_t>::const_iterator itIndex =
                airlineIdToIndex.find(airlineId);
            if (itIndex == airlineIdToIndex.end()) continue;
            const Airline& al = airlines[itIndex->second];

            crow::json::wvalue item;
            item["airline_id"] = al.id;
            item["iata"] = al.iata;
            item["name"] = al.name;
            item["country"] = al.country;
            item["route_count"] = count;

            arr[i] = std::move(item);
        }

        return crow::response{j};
    });

    // Return all airlines ordered by IATA code
    CROW_ROUTE(app, "/airlines-by-iata").methods(crow::HTTPMethod::Get)
    ([](const crow::request&){
        std::vector<const Airline*> v;
        v.reserve(airlines.size());
        for (std::size_t i = 0; i < airlines.size(); ++i) {
            v.push_back(&airlines[i]);
        }

        std::sort(v.begin(), v.end(),
            [](const Airline* a, const Airline* b){
                return a->iata < b->iata;
            });

        crow::json::wvalue j;
        j["airlines"] = crow::json::wvalue::list();
        auto& arr = j["airlines"];

        for (std::size_t i = 0; i < v.size(); ++i) {
            const Airline* al = v[i];
            crow::json::wvalue item;
            item["id"] = al->id;
            item["iata"] = al->iata;
            item["name"] = al->name;
            item["country"] = al->country;
            item["active"] = al->active;
            arr[i] = std::move(item);
        }

        return crow::response{j};
    });

    // Return all airports ordered by IATA code
    CROW_ROUTE(app, "/airports-by-iata").methods(crow::HTTPMethod::Get)
    ([](const crow::request&){
        std::vector<const Airport*> v;
        v.reserve(airports.size());
        for (std::size_t i = 0; i < airports.size(); ++i) {
            v.push_back(&airports[i]);
        }

        std::sort(v.begin(), v.end(),
            [](const Airport* a, const Airport* b){
                return a->iata < b->iata;
            });

        crow::json::wvalue j;
        j["airports"] = crow::json::wvalue::list();
        auto& arr = j["airports"];

        for (std::size_t i = 0; i < v.size(); ++i) {
            const Airport* ap = v[i];
            crow::json::wvalue item;
            item["id"] = ap->id;
            item["iata"] = ap->iata;
            item["name"] = ap->name;
            item["city"] = ap->city;
            item["country"] = ap->country;
            item["latitude"] = ap->latitude;
            item["longitude"] = ap->longitude;
            arr[i] = std::move(item);
        }

        return crow::response{j};
    });

        // ----------- NEW: In-memory update endpoints (extra feature) -----------

    // Add a new airline
    CROW_ROUTE(app, "/airline-add").methods(crow::HTTPMethod::Post)
    ([](const crow::request& req){
        auto body = crow::json::load(req.body);
        if (!body) {
            return crow::response(400, "Invalid JSON");
        }

        if (!body.has("id") || !body.has("iata") || !body.has("name")) {
            return crow::response(400, "Missing required fields: id, iata, name");
        }

        int id = body["id"].i();
        std::string iataRaw = body["iata"].s();
        std::string name = body["name"].s();
        std::string country;
        if (body.has("country")) {
            country = std::string(body["country"].s());
        } else {
            country = "";
        }

        bool active = body.has("active") ? body["active"].b() : true;


        if (airlineIdToIndex.find(id) != airlineIdToIndex.end()) {
            return crow::response(400, "Airline with that ID already exists");
        }

        Airline a;
        a.id = id;
        a.iata = toUpper(trim(iataRaw));
        a.name = trim(name);
        a.country = trim(country);
        a.active = active;

        std::size_t index = airlines.size();
        airlines.push_back(a);

        airlineIdToIndex[a.id] = index;
        if (!a.iata.empty() && a.iata != "\\N") {
            airlineIataToIndex[a.iata] = index;
        }

        crow::json::wvalue resp;
        resp["status"] = "ok";
        resp["message"] = "Airline added in memory";
        resp["airline"]["id"] = a.id;
        resp["airline"]["iata"] = a.iata;
        resp["airline"]["name"] = a.name;
        resp["airline"]["country"] = a.country;
        resp["airline"]["active"] = a.active;

        return crow::response{resp};
    });

    // Update an existing airline (by ID)
    CROW_ROUTE(app, "/airline-update").methods(crow::HTTPMethod::Post)
    ([](const crow::request& req){
        auto body = crow::json::load(req.body);
        if (!body) {
            return crow::response(400, "Invalid JSON");
        }
        if (!body.has("id")) {
            return crow::response(400, "Missing required field: id");
        }

        int id = body["id"].i();
        auto it = airlineIdToIndex.find(id);
        if (it == airlineIdToIndex.end()) {
            return crow::response(404, "Airline ID not found");
        }

        std::size_t index = it->second;
        Airline& a = airlines[index];

        // Keep old IATA to update map if changed
        std::string oldIata = a.iata;

        if (body.has("iata")) {
            a.iata = toUpper(trim(body["iata"].s()));
        }
        if (body.has("name")) {
            a.name = trim(body["name"].s());
        }
        if (body.has("country")) {
            a.country = trim(body["country"].s());
        }
        if (body.has("active")) {
            a.active = body["active"].b();
        }

        // Fix IATA map if changed
        if (a.iata != oldIata) {
            if (!oldIata.empty()) {
                airlineIataToIndex.erase(oldIata);
            }
            if (!a.iata.empty() && a.iata != "\\N") {
                airlineIataToIndex[a.iata] = index;
            }
        }

        crow::json::wvalue resp;
        resp["status"] = "ok";
        resp["message"] = "Airline updated in memory";
        resp["airline"]["id"] = a.id;
        resp["airline"]["iata"] = a.iata;
        resp["airline"]["name"] = a.name;
        resp["airline"]["country"] = a.country;
        resp["airline"]["active"] = a.active;

        return crow::response{resp};
    });

    // Add a new airport
    CROW_ROUTE(app, "/airport-add").methods(crow::HTTPMethod::Post)
    ([](const crow::request& req){
        auto body = crow::json::load(req.body);
        if (!body) {
            return crow::response(400, "Invalid JSON");
        }

        if (!body.has("id") || !body.has("iata") || !body.has("name")) {
            return crow::response(400, "Missing required fields: id, iata, name");
        }

        int id = body["id"].i();
        std::string iataRaw = body["iata"].s();
        std::string name = body["name"].s();
        std::string city;
        if (body.has("city")) {
            city = std::string(body["city"].s());
        } else {
            city = "";
        }

        std::string country;
        if (body.has("country")) {
            country = std::string(body["country"].s());
        } else {
            country = "";
        }

        double lat = body.has("latitude") ? body["latitude"].d() : 0.0;
        double lon = body.has("longitude") ? body["longitude"].d() : 0.0;

        if (airportIdToIndex.find(id) != airportIdToIndex.end()) {
            return crow::response(400, "Airport with that ID already exists");
        }

        Airport a;
        a.id = id;
        a.iata = toUpper(trim(iataRaw));
        a.name = trim(name);
        a.city = trim(city);
        a.country = trim(country);
        a.latitude = lat;
        a.longitude = lon;

        std::size_t index = airports.size();
        airports.push_back(a);

        airportIdToIndex[a.id] = index;
        if (!a.iata.empty() && a.iata != "\\N") {
            airportIataToIndex[a.iata] = index;
        }

        crow::json::wvalue resp;
        resp["status"] = "ok";
        resp["message"] = "Airport added in memory";
        resp["airport"]["id"] = a.id;
        resp["airport"]["iata"] = a.iata;
        resp["airport"]["name"] = a.name;
        resp["airport"]["city"] = a.city;
        resp["airport"]["country"] = a.country;
        resp["airport"]["latitude"] = a.latitude;
        resp["airport"]["longitude"] = a.longitude;

        return crow::response{resp};
    });

    // Update an existing airport (by ID)
    CROW_ROUTE(app, "/airport-update").methods(crow::HTTPMethod::Post)
    ([](const crow::request& req){
        auto body = crow::json::load(req.body);
        if (!body) {
            return crow::response(400, "Invalid JSON");
        }
        if (!body.has("id")) {
            return crow::response(400, "Missing required field: id");
        }

        int id = body["id"].i();
        auto it = airportIdToIndex.find(id);
        if (it == airportIdToIndex.end()) {
            return crow::response(404, "Airport ID not found");
        }

        std::size_t index = it->second;
        Airport& a = airports[index];

        std::string oldIata = a.iata;

        if (body.has("iata")) {
            a.iata = toUpper(trim(body["iata"].s()));
        }
        if (body.has("name")) {
            a.name = trim(body["name"].s());
        }
        if (body.has("city")) {
            a.city = trim(body["city"].s());
        }
        if (body.has("country")) {
            a.country = trim(body["country"].s());
        }
        if (body.has("latitude")) {
            a.latitude = body["latitude"].d();
        }
        if (body.has("longitude")) {
            a.longitude = body["longitude"].d();
        }

        if (a.iata != oldIata) {
            if (!oldIata.empty()) {
                airportIataToIndex.erase(oldIata);
            }
            if (!a.iata.empty() && a.iata != "\\N") {
                airportIataToIndex[a.iata] = index;
            }
        }

        crow::json::wvalue resp;
        resp["status"] = "ok";
        resp["message"] = "Airport updated in memory";
        resp["airport"]["id"] = a.id;
        resp["airport"]["iata"] = a.iata;
        resp["airport"]["name"] = a.name;
        resp["airport"]["city"] = a.city;
        resp["airport"]["country"] = a.country;
        resp["airport"]["latitude"] = a.latitude;
        resp["airport"]["longitude"] = a.longitude;

        return crow::response{resp};
    });

    // Add a new route
    CROW_ROUTE(app, "/route-add").methods(crow::HTTPMethod::Post)
    ([](const crow::request& req){
        auto body = crow::json::load(req.body);
        if (!body) {
            return crow::response(400, "Invalid JSON");
        }

        if (!body.has("airline_id") || !body.has("src_id") || !body.has("dst_id")) {
            return crow::response(400, "Missing required fields: airline_id, src_id, dst_id");
        }

        int airlineId = body["airline_id"].i();
        int srcId = body["src_id"].i();
        int dstId = body["dst_id"].i();
        int stops = body.has("stops") ? body["stops"].i() : 0;

        if (airlineIdToIndex.find(airlineId) == airlineIdToIndex.end()) {
            return crow::response(400, "Unknown airline_id");
        }
        if (airportIdToIndex.find(srcId) == airportIdToIndex.end()) {
            return crow::response(400, "Unknown src_id");
        }
        if (airportIdToIndex.find(dstId) == airportIdToIndex.end()) {
            return crow::response(400, "Unknown dst_id");
        }

        Route r;
        r.airlineId = airlineId;
        r.srcAirportId = srcId;
        r.dstAirportId = dstId;
        r.stops = stops;

        std::size_t index = routes.size();
        routes.push_back(r);
        routesFromSrc[srcId].push_back(index);

        crow::json::wvalue resp;
        resp["status"] = "ok";
        resp["message"] = "Route added in memory";
        resp["route"]["airline_id"] = airlineId;
        resp["route"]["src_id"] = srcId;
        resp["route"]["dst_id"] = dstId;
        resp["route"]["stops"] = stops;

        return crow::response{resp};
    });


    // ----------- NEW: One-hop route report (S -> X -> D, 0 stops) -----------

    CROW_ROUTE(app, "/one-hop").methods(crow::HTTPMethod::Get)
    ([](const crow::request& req){
        const char* srcParam = req.url_params.get("src");
        const char* dstParam = req.url_params.get("dst");
        if (!srcParam || !dstParam) {
            return crow::response(400, "Missing 'src' or 'dst' query parameter");
        }

        std::string srcIata = srcParam;
        std::string dstIata = dstParam;

        const Airport* srcAirport = findAirportByIata(srcIata);
        const Airport* dstAirport = findAirportByIata(dstIata);

        if (!srcAirport) {
            return crow::response(404, "Source airport not found");
        }
        if (!dstAirport) {
            return crow::response(404, "Destination airport not found");
        }

        int srcId = srcAirport->id;
        int dstId = dstAirport->id;

        struct OneHopRoute {
            int viaAirportId;
            double leg1Miles;
            double leg2Miles;
            double totalMiles;
            const Airline* airline1;
            const Airline* airline2;
        };

        std::vector<OneHopRoute> results;

        // First leg: srcId -> mid
        std::unordered_map<int, std::vector<std::size_t> >::const_iterator itAdj =
            routesFromSrc.find(srcId);
        if (itAdj != routesFromSrc.end()) {
            const std::vector<std::size_t>& outRoutes = itAdj->second;
            for (std::size_t i = 0; i < outRoutes.size(); ++i) {
                const Route& r1 = routes[outRoutes[i]];
                if (r1.stops != 0) continue; // must be 0 stops

                int midId = r1.dstAirportId;
                const Airport* midAirport = findAirportById(midId);
                if (!midAirport) continue;

                // Second leg: mid -> dstId
                std::unordered_map<int, std::vector<std::size_t> >::const_iterator itAdj2 =
                    routesFromSrc.find(midId);
                if (itAdj2 == routesFromSrc.end()) continue;

                const std::vector<std::size_t>& outRoutes2 = itAdj2->second;
                for (std::size_t j = 0; j < outRoutes2.size(); ++j) {
                    const Route& r2 = routes[outRoutes2[j]];
                    if (r2.stops != 0) continue;
                    if (r2.dstAirportId != dstId) continue;

                    const Airline* al1 = findAirlineById(r1.airlineId);
                    const Airline* al2 = findAirlineById(r2.airlineId);

                    double leg1 = greatCircleMiles(*srcAirport, *midAirport);
                    double leg2 = greatCircleMiles(*midAirport, *dstAirport);

                    OneHopRoute oh;
                    oh.viaAirportId = midId;
                    oh.leg1Miles = leg1;
                    oh.leg2Miles = leg2;
                    oh.totalMiles = leg1 + leg2;
                    oh.airline1 = al1;
                    oh.airline2 = al2;

                    results.push_back(oh);
                }
            }
        }

        std::sort(results.begin(), results.end(),
                  [](const OneHopRoute& a, const OneHopRoute& b){
                      return a.totalMiles < b.totalMiles;
                  });

        crow::json::wvalue j;
        // source & destination info
        j["source"]["id"] = srcAirport->id;
        j["source"]["iata"] = srcAirport->iata;
        j["source"]["name"] = srcAirport->name;
        j["source"]["city"] = srcAirport->city;
        j["source"]["country"] = srcAirport->country;

        j["destination"]["id"] = dstAirport->id;
        j["destination"]["iata"] = dstAirport->iata;
        j["destination"]["name"] = dstAirport->name;
        j["destination"]["city"] = dstAirport->city;
        j["destination"]["country"] = dstAirport->country;

        j["routes"] = crow::json::wvalue::list();
        auto& arr = j["routes"];

        for (std::size_t i = 0; i < results.size(); ++i) {
            const OneHopRoute& oh = results[i];
            const Airport* midAirport = findAirportById(oh.viaAirportId);
            if (!midAirport) continue;

            crow::json::wvalue item;
            item["via"]["id"] = midAirport->id;
            item["via"]["iata"] = midAirport->iata;
            item["via"]["name"] = midAirport->name;
            item["via"]["city"] = midAirport->city;
            item["via"]["country"] = midAirport->country;

            item["leg1_miles"] = oh.leg1Miles;
            item["leg2_miles"] = oh.leg2Miles;
            item["total_miles"] = oh.totalMiles;

            if (oh.airline1) {
                item["airline1"]["id"] = oh.airline1->id;
                item["airline1"]["iata"] = oh.airline1->iata;
                item["airline1"]["name"] = oh.airline1->name;
            }
            if (oh.airline2) {
                item["airline2"]["id"] = oh.airline2->id;
                item["airline2"]["iata"] = oh.airline2->iata;
                item["airline2"]["name"] = oh.airline2->name;
            }

            arr[i] = std::move(item);
        }

        return crow::response{j};
    });

    // ----------- NEW: Get Code (returns main.cpp contents) -----------

    CROW_ROUTE(app, "/get-code")
    ([](){
        std::ifstream in("main.cpp");
        if (!in) {
            return crow::response(500, "Could not open main.cpp");
        }
        std::stringstream buffer;
        buffer << in.rdbuf();
        crow::json::wvalue j;
        j["filename"] = "main.cpp";
        j["code"] = buffer.str();
        return crow::response{j};
    });

    // Start server on port 8080
    app.port(8080).multithreaded().run();

    return 0;
}
