#undef UNICODE

#include <array>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <strstream>
#include <vector>
#include <winsock2.h>

using namespace std;

// link with Ws2_32.lib
#pragma comment( lib, "ws2_32.lib" )

// Socket params
typedef struct {
	SOCKET socket;
	char* ip;
	USHORT port;
} socketParams;

// External functions
void				onServerBoot();
bool				onConnectionRequest();
extern DWORD WINAPI voteProcessing(void* sd_);
bool				processVote(const array<char, 64>& voteBuffer);
void				logInfo(socketParams* params, bool valid);
void				onPollOver();
string				itos(int i);

// Constants
const char*					LOGFILE = "journal.txt";
const char*					CANDIDATES_LIST = "Liste_des_candidats.txt";
const timeval				SELECT_TIMEOUT{0, 10000};
const chrono::milliseconds	DURATION(1000);

// Variables
SOCKET										sd;
atomic<int>									nbSockets;
string										strCandidates = "";
map<int, string>							candidates;
map<int, int>								votes;
int											nbVotes;
chrono::time_point<chrono::system_clock>	endTime;


// List of Winsock error constants mapped to an interpretation string.
// Note that this list must remain sorted by the error constants'
// values, because we do a binary search on the list when looking up
// items.
static struct ErrorEntry {
    int nID;
    const char* pcMessage;

    ErrorEntry(int id, const char* pc = 0) : 
    nID(id), 
    pcMessage(pc) 
    { 
    }

    bool operator<(const ErrorEntry& rhs) const
    {
        return nID < rhs.nID;
    }
} gaErrorList[] = {
    ErrorEntry(0,                  "No error"),
    ErrorEntry(WSAEINTR,           "Interrupted system call"),
    ErrorEntry(WSAEBADF,           "Bad file number"),
    ErrorEntry(WSAEACCES,          "Permission denied"),
    ErrorEntry(WSAEFAULT,          "Bad address"),
    ErrorEntry(WSAEINVAL,          "Invalid argument"),
    ErrorEntry(WSAEMFILE,          "Too many open sockets"),
    ErrorEntry(WSAEWOULDBLOCK,     "Operation would block"),
    ErrorEntry(WSAEINPROGRESS,     "Operation now in progress"),
    ErrorEntry(WSAEALREADY,        "Operation already in progress"),
    ErrorEntry(WSAENOTSOCK,        "Socket operation on non-socket"),
    ErrorEntry(WSAEDESTADDRREQ,    "Destination address required"),
    ErrorEntry(WSAEMSGSIZE,        "Message too long"),
    ErrorEntry(WSAEPROTOTYPE,      "Protocol wrong type for socket"),
    ErrorEntry(WSAENOPROTOOPT,     "Bad protocol option"),
    ErrorEntry(WSAEPROTONOSUPPORT, "Protocol not supported"),
    ErrorEntry(WSAESOCKTNOSUPPORT, "Socket type not supported"),
    ErrorEntry(WSAEOPNOTSUPP,      "Operation not supported on socket"),
    ErrorEntry(WSAEPFNOSUPPORT,    "Protocol family not supported"),
    ErrorEntry(WSAEAFNOSUPPORT,    "Address family not supported"),
    ErrorEntry(WSAEADDRINUSE,      "Address already in use"),
    ErrorEntry(WSAEADDRNOTAVAIL,   "Can't assign requested address"),
    ErrorEntry(WSAENETDOWN,        "Network is down"),
    ErrorEntry(WSAENETUNREACH,     "Network is unreachable"),
    ErrorEntry(WSAENETRESET,       "Net connection reset"),
    ErrorEntry(WSAECONNABORTED,    "Software caused connection abort"),
    ErrorEntry(WSAECONNRESET,      "Connection reset by peer"),
    ErrorEntry(WSAENOBUFS,         "No buffer space available"),
    ErrorEntry(WSAEISCONN,         "Socket is already connected"),
    ErrorEntry(WSAENOTCONN,        "Socket is not connected"),
    ErrorEntry(WSAESHUTDOWN,       "Can't send after socket shutdown"),
    ErrorEntry(WSAETOOMANYREFS,    "Too many references, can't splice"),
    ErrorEntry(WSAETIMEDOUT,       "Connection timed out"),
    ErrorEntry(WSAECONNREFUSED,    "Connection refused"),
    ErrorEntry(WSAELOOP,           "Too many levels of symbolic links"),
    ErrorEntry(WSAENAMETOOLONG,    "File name too long"),
    ErrorEntry(WSAEHOSTDOWN,       "Host is down"),
    ErrorEntry(WSAEHOSTUNREACH,    "No route to host"),
    ErrorEntry(WSAENOTEMPTY,       "Directory not empty"),
    ErrorEntry(WSAEPROCLIM,        "Too many processes"),
    ErrorEntry(WSAEUSERS,          "Too many users"),
    ErrorEntry(WSAEDQUOT,          "Disc quota exceeded"),
    ErrorEntry(WSAESTALE,          "Stale NFS file handle"),
    ErrorEntry(WSAEREMOTE,         "Too many levels of remote in path"),
    ErrorEntry(WSASYSNOTREADY,     "Network system is unavailable"),
    ErrorEntry(WSAVERNOTSUPPORTED, "Winsock version out of range"),
    ErrorEntry(WSANOTINITIALISED,  "WSAStartup not yet called"),
    ErrorEntry(WSAEDISCON,         "Graceful shutdown in progress"),
    ErrorEntry(WSAHOST_NOT_FOUND,  "Host not found"),
    ErrorEntry(WSANO_DATA,         "No host data of that type was found")
};
const int kNumMessages = sizeof(gaErrorList) / sizeof(ErrorEntry);


//// WSAGetLastErrorMessage ////////////////////////////////////////////
// A function similar in spirit to Unix's perror() that tacks a canned 
// interpretation of the value of WSAGetLastError() onto the end of a
// passed string, separated by a ": ".  Generally, you should implement
// smarter error handling than this, but for default cases and simple
// programs, this function is sufficient.
//
// This function returns a pointer to an internal static buffer, so you
// must copy the data from this function before you call it again.  It
// follows that this function is also not thread-safe.
const char* WSAGetLastErrorMessage(const char* pcMessagePrefix, int nErrorID = 0)
{
    // Build basic error string
    static char acErrorBuffer[256];
    ostrstream outs(acErrorBuffer, sizeof(acErrorBuffer));
    outs << pcMessagePrefix << ": ";

    // Tack appropriate canned message onto end of supplied message 
    // prefix. Note that we do a binary search here: gaErrorList must be
	// sorted by the error constant's value.
	ErrorEntry* pEnd = gaErrorList + kNumMessages;
    ErrorEntry Target(nErrorID ? nErrorID : WSAGetLastError());
    ErrorEntry* it = lower_bound(gaErrorList, pEnd, Target);
    if ((it != pEnd) && (it->nID == Target.nID)) {
        outs << it->pcMessage;
    }
    else {
        // Didn't find error in list, so make up a generic one
        outs << "unknown error";
    }
    outs << " (" << Target.nID << ")";

    // Finish error message off and return it.
    outs << ends;
    acErrorBuffer[sizeof(acErrorBuffer) - 1] = '\0';
    return acErrorBuffer;
}

int main(void) 
{
	//----------------------
	// Initialize Winsock.
	WSADATA wsaData;
	int iResult = WSAStartup(MAKEWORD(2,2), &wsaData);
	if (iResult != NO_ERROR) {
		cerr << "Error at WSAStartup()\n" << endl;
		return 1;
	}

	//----------------------
	// Create a SOCKET for listening for
	// incoming connection requests.
	SOCKET ServerSocket;
	ServerSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (ServerSocket == INVALID_SOCKET) {
        cerr << WSAGetLastErrorMessage("Error at socket()") << endl;
		WSACleanup();
		return 1;
	}
	char* option = "1";
	setsockopt(ServerSocket, SOL_SOCKET, SO_REUSEADDR, option, sizeof(option));

    //----------------------
    // The sockaddr_in structure specifies the address family,
    // IP address, and port for the socket that is being bound.
	int port=5000;
    
	//Recuperation de l'adresse locale
	hostent *thisHost;
	thisHost=gethostbyname("132.207.29.108");
	char* ip;
	ip=inet_ntoa(*(struct in_addr*) *thisHost->h_addr_list);
	printf("Adresse locale trouvee %s : \n\n",ip);
	sockaddr_in service;
    service.sin_family = AF_INET;
	service.sin_addr.s_addr = inet_addr(ip);
    service.sin_port = htons(port);

    if (bind(ServerSocket, (SOCKADDR*) &service, sizeof(service)) == SOCKET_ERROR) {
		cerr << WSAGetLastErrorMessage("bind() failed.") << endl;
		closesocket(ServerSocket);
		WSACleanup();
		return 1;
	}
	
	//----------------------
	// Listen for incoming connection requests.
	// on the created socket
	if (listen(ServerSocket, 30) == SOCKET_ERROR) {
		cerr << WSAGetLastErrorMessage("Error listening on socket.") << endl;
		closesocket(ServerSocket);
		WSACleanup();
		return 1;
	}


	printf("En attente des connections des clients sur le port %d...\n\n",ntohs(service.sin_port));

	onServerBoot();

	// Loop while polling is open
    while (endTime.time_since_epoch().count() == 0 || chrono::system_clock::now().time_since_epoch().count() < endTime.time_since_epoch().count()) {
		sockaddr_in sinRemote;
		int nAddrSize = sizeof(sinRemote);

		fd_set readSet;	// source : http://www.gamedev.net/topic/590497-winsock-c-accept-non-blocking/
		FD_ZERO(&readSet);
		FD_SET(ServerSocket, &readSet);

		// Wait for connections, non-blocking
		if (select(ServerSocket, &readSet, NULL, NULL, &SELECT_TIMEOUT) > 0) {	// source : https://msdn.microsoft.com/en-us/library/windows/desktop/ms740141%28v=vs.85%29.aspx
			// Accept the connection.
			SOCKET sd = accept(ServerSocket, (sockaddr*)&sinRemote, &nAddrSize);
			if (sd != INVALID_SOCKET && onConnectionRequest()) {
				++nbSockets;
				cout << "Connection acceptee De : " <<
					inet_ntoa(sinRemote.sin_addr) << ":" <<
					ntohs(sinRemote.sin_port) << "." <<
					endl;

				socketParams params{ sd, inet_ntoa(sinRemote.sin_addr), ntohs(sinRemote.sin_port) };
				DWORD nThreadID;
				CreateThread(0, 0, voteProcessing, (void*)&params, 0, &nThreadID);
			}
			else {
				cerr << WSAGetLastErrorMessage("Echec d'une connection.") <<
					endl;
			}
		}
    }

	// Polling is now closed
	// Waiting for remaining voters
	while (nbSockets > 0) {}

	onPollOver();
  
    // No longer need server socket
	closesocket(ServerSocket);

    WSACleanup();
    return 0;
}


//// voteProcessing ///////////////////////////////////////////////////////
// Processes the voters connection and information

DWORD WINAPI voteProcessing(void* sd_) 
{
	auto params = reinterpret_cast<socketParams*>(sd_);
	SOCKET sd = (SOCKET)params->socket;
	send(sd, strCandidates.c_str(), strCandidates.size(), 0);

	// Receive vote
	array<char, 64> voteBuffer;

	recv(sd, voteBuffer.data(), voteBuffer.size(), 0);
	
	bool valid = processVote(voteBuffer);
	logInfo(params, valid);

	// Send reception confirmation
	const char* validVote = "1";
	send(sd, validVote, 1, 0);

	closesocket(sd);
	--nbSockets;

	return 0;
}

//// onServerBoot ///////////////////////////////////////////////////////
// Initializes server variables on boot
void onServerBoot() {
	// Init counter
	nbSockets = 0;

	// Read the candidates file
	ifstream candidatesFile(CANDIDATES_LIST);

	// Cleanup logfile
	ofstream file;
	file.open(LOGFILE, std::ofstream::out | std::ofstream::trunc);
	file.clear();
	file.close();

	// Insert the data in map candidates
	while (!candidatesFile.eof() && !candidatesFile.fail()) {
		string candidate;
		getline(candidatesFile, candidate, ';');
		candidate.erase(std::remove(candidate.begin(), candidate.end(), '\n'), candidate.end());
		auto nb = candidates.size();
		if (candidate != "") {
			candidates.emplace(nb, candidate);
			votes.emplace(nb, 0);
			strCandidates += itos(nb) + " : " + candidates.at(nb) + "\n";
		}
	}
}

//// processVote ///////////////////////////////////////////////////////
// Computes a vote
bool processVote(const array<char, 64>& voteBuffer) {
	// Trunk unecessary characters in the buffer
	auto delimiterPos = std::find(voteBuffer.begin(), voteBuffer.end(), '\0');

	// Check if vote is valid
	bool valid = false;
	if (all_of(voteBuffer.begin(), delimiterPos, [](char c) { return c >= '0' && c <= '9'; })) {
		std::string candidateStr = { voteBuffer.begin(), delimiterPos };

		// Add vote to candidate
		auto candidateId = stoi(candidateStr);
		if (candidates.find(candidateId) != candidates.end()) {
			++votes.at(candidateId);
			valid = true;
		}
	}
	++nbVotes;
	return valid;
}

//// logInfo ///////////////////////////////////////////////////////
// Logs the voters infos in the log file
void logInfo(socketParams* params, bool valid) {
	ofstream file;
	file.open(LOGFILE, std::ofstream::out | std::ofstream::app);
	string ip = params->ip;
	string port = itos(params->port);
	time_t now = chrono::system_clock::to_time_t(chrono::system_clock::now());
	string now_s = ctime(&now);
	now_s.resize(now_s.size() - 1);

	file << now_s << " -- " << ip << ":" << port << " -- " << (valid ? "valide" : "invalide") << endl;
	file.close();
}

//// onPollOver ///////////////////////////////////////////////////////
// Handles what to do after the voting process is complete
void onPollOver() {
	cout << "*** VOTES TERMINES ***" << endl;

	// Create the sortable vector
	vector<pair<string, int>> results;
	for (auto i : candidates) {
		results.emplace_back(i.second, votes.at(i.first));
	}

	// Sort candidates by votes
	sort(results.begin(), results.end(), [](pair<string, int> p1, pair<string, int> p2) {return p1.second > p2.second;});

	// Print the results
	int nbValidVotes = 0;
	for (auto r : results) {
		nbValidVotes += r.second;
		cout << r.first << "\t: " << r.second << " (" << 100.0 * r.second / nbVotes << "%)" << endl;
	}
	int nbInvalidVotes = nbVotes - nbValidVotes;
	cout << "Votes invalides \t: " << nbInvalidVotes << " (" << 100.0 * nbInvalidVotes / nbVotes << "%)" << endl;
	cout << endl << "Nombre total d'electeurs : " << nbVotes << endl;
}

//// onConnectionRequest ///////////////////////////////////////////////////////
// Checks if a connection can be accepted or not (returns true if accepted; false otherwise)
bool onConnectionRequest() {
	// Initialize countdown if not initialized
	if (endTime.time_since_epoch().count() == 0) {
		endTime = chrono::system_clock::now() + DURATION;
	}

	// Accept connection if poll is open
	if (chrono::system_clock::now().time_since_epoch().count() < endTime.time_since_epoch().count()) {
		return true;
	}
	return false;
}

//// itos ///////////////////////////////////////////////////////
// Converts an integer to string format
string itos(int i) {
	stringstream ss;
	ss << i;
	string s;
	ss >> s;
	return s;
}
