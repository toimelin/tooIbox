#include "Logger.h"
#include "ConfigManager.h"
#include <mutex>
#include <fstream>
#include <sstream>
#include <iomanip>

#define LOG_CFG_PATH_FILE "../../../cfg/logging.cfg"
#define LOGINTEREST_RELATIVE_PATH_ROOT "../../../cfg/"

using std::cerr;
using std::endl;
using std::cout;
namespace Logger {
	std::map<std::string, LogType> LogTypes; // Cannot be rMaps since memory allocator is initialized after logger since logger is a namespace only // TODODP: See if anything can be done about this
	std::map<std::string, InterestEntry> InterestedLogTypes;
	std::map<LogSeverity::BitFlag, std::pair<uint32_t, std::stringstream>> SeverityOutputs;

	bool Timestamp = true;
	std::string TimestampFormat = "%c";
	bool HighPrecisionStamp = false;
	unsigned int TreeHeight = 1;

	unsigned int FileFlushAtNrOfLines = 20;
	std::vector<std::string> LinesToFlush;
	std::string LogFile = "log.txt";
	std::mutex FileFlushMutex;
	bool LogToFile = false;
	bool AppendToFile = true;

	std::string NameSeparator = ".";
	bool IWantItAllFlag = false;

	Uint64 StartTime = SDL_GetPerformanceCounter();

	rString GetParentString ( const rString& name, unsigned int treeHeight );
	
	std::stringstream OutputStream;
	
	std::stringstream& GetStream ( ) { return OutputStream; }
	std::stringstream& GetSeverityOutputStream( LogSeverity::BitFlag logSeverity ) { return SeverityOutputs.at( logSeverity ).second; }
	uint32_t GetSeverityOutputUnreadCount( LogSeverity::BitFlag logseverity ) { return SeverityOutputs.at( logseverity ).first; }
	void MarkSeverityOutputStreamRead( LogSeverity::BitFlag logSeverity ) { SeverityOutputs.at( logSeverity ).first = 0; }
	
	std::mutex Mutex;
}

/// <summary>
/// Initializes the logger by reading the config file LOG_CFG_PATH_FILE.
/// <para>This should be called after registering types.</para>
/// </summary>
void Logger::Initialize() {
	CallbackConfig* cfg = g_ConfigManager.GetConfig( LOG_CFG_PATH_FILE );

	HighPrecisionStamp = cfg->GetBool ( "HighPrecisionStamp", false, "Whether to print high precision stamp or not" );
	TreeHeight = static_cast<unsigned int> ( cfg->GetInt ( "TreeHeight", 3, "How deep to print parent scope" ) );
	NameSeparator = cfg->GetString ( "NameSeparator", ".", "What separates names when printing the log scope" ).c_str( );
	IWantItAllFlag = cfg->GetBool ( "IWantItAll", true, "Whether to log all output or not" );
	Timestamp = cfg->GetBool ( "Timestamp", false, "Whether to print time and date or not" );
	TimestampFormat = cfg->GetString ( "TimestampFormat", "%H:%M:%S %d-%m-%Y", "How to print the time and date" );
	FileFlushAtNrOfLines = static_cast<unsigned int>( cfg->GetInt( "FileFlushAtNrOfLines", 100, "Will flush the log to file when this number of lines are in the buffer" ) );
	LogFile = cfg->GetString( "LogFile", "log.txt", "Which file to log to" );
	LogToFile = cfg->GetBool( "LogToFile", false, "Whether to log to file or not" );
	AppendToFile = cfg->GetBool( "AppendToFile", true, "Appends output to file. Otherwise overwrite it" );
	if ( IWantItAllFlag ) {
		IWantItAll();
	}

	SeverityOutputs.emplace( LogSeverity::ERROR_MSG,	std::pair<uint32_t, std::stringstream>( 0, std::stringstream() ) );
	SeverityOutputs.emplace( LogSeverity::WARNING_MSG,	std::pair<uint32_t, std::stringstream>( 0, std::stringstream() ) );
	SeverityOutputs.emplace( LogSeverity::INFO_MSG,		std::pair<uint32_t, std::stringstream>( 0, std::stringstream() ) );
	SeverityOutputs.emplace( LogSeverity::DEBUG_MSG,	std::pair<uint32_t, std::stringstream>( 0, std::stringstream() ) );
	FlushToFile( false );
}

/// <summary>
/// Cleanup logger structures.
/// </summary>
void Logger::Cleanup( ) {
	FlushToFile();
	LogTypes.clear( );
	InterestedLogTypes.clear( );
}

/// <summary>
/// Reads a file containing types of logging messages that the user is interested in.
/// <para>If a path is not supplied it will read the path from the config file
/// defined in LOG_CFG_PATH_FILE.</para>
/// <para>Will not be run if IWantItAll setting is true</para>
/// </summary>
/// <param name="path">Path to the file to be read (optional)</param>
void Logger::RegisterInterestFromFile ( const rString& path ) {
	if ( IWantItAllFlag ) {
		return;
	}
	rString pathToLoad = path;
	// Read path from file if none was supplied
	if ( path == "" ) {
		CallbackConfig* cfg = g_ConfigManager.GetConfig( LOG_CFG_PATH_FILE );
		if ( cfg != nullptr ) {
			pathToLoad = LOGINTEREST_RELATIVE_PATH_ROOT + cfg->GetString ( "InterestFile", "logging/alldeep.cfg" );
		} else {		
			cerr << ConsoleColors::BRed << "Failed to read logging config file: " << LOG_CFG_PATH_FILE << ConsoleColors::Reset
				 << endl;
			return;
		}
	}
	CallbackConfig* cfgToLoad = g_ConfigManager.GetConfig( pathToLoad );
	if ( cfgToLoad != nullptr ) {
		rMap<rString, Config::ConfigEntry*>* m = cfgToLoad->GetScopeMap ( "LogInterests" );
		if ( m ) {
			// Loop through every map entry in the config
			for ( auto& entry : *m ) {
				RegisterInterest ( entry.first, entry.second->Value.IntVal );
			}
			Log( "Read logging interest file: " + pathToLoad, "Logger", LogSeverity::INFO_MSG );
		} else {
			cerr << ConsoleColors::BRed << "Failed to read logging interest file: " << LOGINTEREST_RELATIVE_PATH_ROOT
				 << ConsoleColors::Reset << endl;
		}
	} else {
		std::cerr << "Failed to parse logger interest config file: " << path << std::endl;
	}
}

/// <summary>
/// Registers a type of logging message.
/// <para>This function should be run once for each type of message the programmer creates.</para>
/// <para>If a parent is supplied the supplied <paramref name="name"/> will be a child to this parent. If a parent
/// is registered with <see cref="Logger::RegisterInterest"/> the childs logging messages will
/// also be seen.</para>
/// <para><paramref name="parent"/> can be left empty if a parent is not desired.</para>
/// <remarks>The parent name supplied with <paramref name="parent"/> needs to be registered before
/// any children can be added</remarks>
/// </summary>
/// <param name="name">Name of the logging message</param>
/// <param name="parent">Parent of the supplied name (optional)</param>
void Logger::RegisterType ( const rString& name, const rString& parent ) {
	if ( parent != "" ) {
		// If a parent is supplied, find it
		rMap<std::string, LogType>::iterator it = LogTypes.find ( parent.c_str( ) );

		// Check if parent was found
		if ( it != LogTypes.end() ) {
			// Create LogType, emplace it and record the child in the parent.
			LogType lt;
			lt.Parent = parent;
			LogTypes.emplace ( name.c_str( ), lt );
			it->second.Children.push_back ( name );
		} else {
			cerr << ConsoleColors::BRed << "Failed to find parent log type for log type: " << name << ConsoleColors::Reset <<
				 endl;
			return;
		}
	} else {
		// No parent was supplied so create LogType with no parent.
		LogType lt;
		lt.Parent = "";
		LogTypes.emplace ( name.c_str( ), lt );
	}
}

/// <summary>
/// Registers interest in a type of logging message that has been registered with
/// <see cref="Logger::RegisterType"/>.
/// <para>If a parent is registered all its children will also be registered</para>
/// </summary>
/// <param name="name">Path to the file to be read</param>
/// <param name="severityMask">Bitmask of type <see cref="LogSeverity::BitFlag"/></param>
void Logger::RegisterInterest ( const rString& name, int severityMask ) {
	rMap<std::string, LogType>::iterator it = LogTypes.find ( name.c_str( ) );
	if ( it != LogTypes.end() ) {
		// Create interest entry with supplied severitymask and name.
		InterestEntry ie;
		ie.Parent = it->second.Parent;
		ie.SeverityMask = severityMask;
		InterestedLogTypes.emplace ( name.c_str( ), ie );
		// Add all children as well.
		for ( rVector<rString>::iterator chit = it->second.Children.begin();
			  chit != it->second.Children.end(); ++chit ) {
			RegisterInterest ( *chit, severityMask );
		}
	} else {
		cerr 	<< ConsoleColors::BRed << "Failed to find log type " << name
				<< " interest not registered." << ConsoleColors::Reset << endl;
	}
}

/// <summary>
/// Registers interest in all types of registered logging messages.
/// </summary>
/// <param name="severityMask">Bitmask of type <see cref="LogSeverity::BitFlag"/></param>
void Logger::IWantItAll ( int severityMask ) {
	for ( auto& kv : LogTypes ) {
		InterestEntry ie;
		ie.Parent = kv.second.Parent;
		ie.SeverityMask = severityMask;
		InterestedLogTypes.emplace ( kv.first, ie );
	}
}

/// <summary>
/// Logs a message in the logger.
/// <para>Logs a message in the logger with the selected type, message and severity.</para>
/// <para>Use <see cref="Logger::RegisterType"/> to register types for use with the logger.</para>
/// <para>Use <see cref="Logger::RegisterInterest"/> to register interest in a type of
/// log message that has been registered.</para>
/// </summary>
/// <param name="message">A string containing the message to be displayed</param>
/// <param name="type">A string defining what type of message it is. (ex. Network or Game)</param>
/// <param name="logSeve">The severity of the message to be logged</param>
void Logger::Log ( const rString& message, const rString& type, LogSeverity::BitFlag logSev ) {
	rMap<std::string, InterestEntry>::const_iterator cit = InterestedLogTypes.find ( type.c_str( ) );
	// Don't log things we are not interested in
	if ( cit == InterestedLogTypes.end() ) {
		if ( LogTypes.find( type ) == LogTypes.end( ) ) {
			std::cerr << ConsoleColors::BRed << "!!!LOGGER ERROR. Tried to print message with invalid type: \"" << type <<
				"\". Make sure to register the type" << ". Message: " << message << std::endl;
		}
		return;
	} else if ( !( cit->second.SeverityMask & logSev ) ) {
		return;
	}

	rOStringStream oss;
	rOStringStream oss2; //Used by the other output stream

	if ( Timestamp ) {
		std::time_t now = std::chrono::system_clock::to_time_t( std::chrono::system_clock::now() );
		oss << std::put_time( std::localtime( &now ), TimestampFormat.c_str() ) << " ";
	}
	if ( HighPrecisionStamp ) {
		oss	<< ConsoleColors::Yellow << SDL_GetPerformanceCounter() - StartTime << " ";
		oss2 << "<C=YELLOW>" << SDL_GetPerformanceCounter() - StartTime << " ";
	}

	switch ( logSev ) {
		case LogSeverity::INFO_MSG: {
			oss << ConsoleColors::White << "INFO ";
			oss2 << "[C=WHITE]" << "INFO ";
			SeverityOutputs.at( LogSeverity::INFO_MSG ).second << GetParentString( cit->first.c_str(), TreeHeight ) << ": " << message << endl;
			SeverityOutputs.at( LogSeverity::INFO_MSG ).first++;
			break;
		}

		case LogSeverity::WARNING_MSG: {
			oss << ConsoleColors::BPurple << "WARNING ";
			oss2 << "[C=PURPLE]" << "WARNING ";
			SeverityOutputs.at( LogSeverity::WARNING_MSG ).second << GetParentString( cit->first.c_str(), TreeHeight ) << ": " << message << endl;
			SeverityOutputs.at( LogSeverity::WARNING_MSG ).first++;
			break;
		}

		case LogSeverity::ERROR_MSG: {
			oss << ConsoleColors::BRed << "ERROR ";
			oss2 << "[C=RED]" << "ERROR ";
			SeverityOutputs.at( LogSeverity::ERROR_MSG ).second << GetParentString( cit->first.c_str(), TreeHeight ) << ": " << message << endl;
			SeverityOutputs.at( LogSeverity::ERROR_MSG ).first++;
			break;
		}

		case LogSeverity::DEBUG_MSG: {
			oss << ConsoleColors::Green << "DEBUG ";
			oss2 << "[C=GREEN]" << "DEBUG ";
			SeverityOutputs.at( LogSeverity::DEBUG_MSG ).second << GetParentString( cit->first.c_str(), TreeHeight ) << ": " << message << endl;
			SeverityOutputs.at( LogSeverity::DEBUG_MSG ).first++;
			break;
		}

		case LogSeverity::ALL: {
			oss << "WRONG_SEVERITY_DO_NOT_USE ";
			oss2 << "WRONG_SEVERITY_DO_NOT_USE ";
			break;
		}
	}
	oss << GetParentString ( cit->first.c_str( ), TreeHeight ) << ": " << message << ConsoleColors::Reset << endl;
	cout << oss.str();
	FileFlushMutex.lock();
	LinesToFlush.push_back( oss.str() );
	if ( LinesToFlush.size() >= FileFlushAtNrOfLines ) {
		FlushToFile( true );
	}
	FileFlushMutex.unlock();
	
	Mutex.lock(); //Needed because it doesn't seem to be threadsafe. It crashes randomly at startup on Linux.
	oss2 << GetParentString ( cit->first.c_str( ), TreeHeight ) << ": " << message << endl;
	OutputStream << oss2.str();
	Mutex.unlock();
}

rString Logger::GetParentString ( const rString& name, unsigned int treeHeight ) {
	if ( treeHeight == 0 ) {
		return "";
	}
	rMap<std::string, LogType>::const_iterator pit = LogTypes.find ( name.c_str( ) );
	if ( pit == LogTypes.end() ) {
		return name;
	} else if( pit->second.Parent == "" ) {
		return name;
	}
	return GetParentString ( pit->second.Parent, treeHeight - 1 ) + NameSeparator.c_str( ) + name;
}

void Logger::FlushToFile( bool append ) {
	std::ofstream file;
	if ( AppendToFile || append ) {
		file.open( LogFile, std::ios::app | std::ios::out );
	} else {
		file.open( LogFile, std::ios::out );
	}
	for ( const auto& line : LinesToFlush ) {
		file << line;
	}
	file.close();
	LinesToFlush.clear();
}
