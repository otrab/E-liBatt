#include <DataExchanger.h>

Message::~Message() {
}

size_t Message::writeTo(DataStreamWriter *dsw) {
    size_t r = 0;
    r += dsw->writeInt16(m_crc);
    r += dsw->writeByte(m_type);
    r += dsw->writeInt32(m_fromId);
    r += dsw->writeInt32(m_targetId);
    r += dsw->writeArray(m_data, CUSTOM_MESSAGE_DATA_LENGTH);
    if (r != MESSAGE_SIZE) {
        return -1;
    }
    return r;
}

size_t Message::readFrom(DataStreamReader *dsr) {
    bool ok = true;

    m_crc = dsr->readInt16(&ok);
    size_t r = 2;
    if (!ok) return -1;

    m_type = dsr->readByte(&ok);
    r++;
    if (!ok) return -1;

    m_fromId = dsr->readInt32(&ok);
    r += 4;
    if (!ok) return -1;

    m_targetId = dsr->readInt32(&ok);
    r += 4;
    if (!ok) return -1;

    dsr->readFully(m_data, CUSTOM_MESSAGE_DATA_LENGTH, &ok);
    r += CUSTOM_MESSAGE_DATA_LENGTH;
    if (!ok) return -1;

    if (r != MESSAGE_SIZE) {
        return -1;
    }
    return r;
}

void Message::clearData() {
	for(byte i = 0; i < CUSTOM_MESSAGE_DATA_LENGTH; i++) {
		m_data[i] = 0;
	}
}

void Message::calculateAndSetCrc() {
    byte buffer[MESSAGE_SIZE-2];
    buffer[0] = m_type;
    Utils::toByte(m_fromId, buffer+1);
    Utils::toByte(m_targetId, buffer+5);
    Utils::copyArray(m_data, buffer+9, CUSTOM_MESSAGE_DATA_LENGTH);
    m_crc = SimpleCrc::crc16(buffer, MESSAGE_SIZE-2);
}

Handler::~Handler() {
}

void DataExchanger::process(
        Message &message,                 // Message received.
        DataStreamWriter *readFromLine,   // Communication line where the message was read from.
        DataStreamWriter *opposingLine) { // Opposing communication line, to crosstalk messages.

	switch(message.m_type) {
	case SCAN_MESSAGE:
		// Transmit the same unaddressed scan message to the next in the chain.
		transmit(opposingLine, message);

		// Swap ids to send the message back (I'm sending a message addressed to master).
		message.m_targetId = message.m_fromId;
		message.m_fromId = m_id;
		message.m_type = SCAN_MESSAGE_RESPONSE;

		// Send back.
		message.calculateAndSetCrc();
		transmit(readFromLine, message);

		break;

	case SCAN_ID_READ:
		// Swap ids to send the message back (I'm sending a message addressed to master).
		message.m_targetId = message.m_fromId;
		message.m_fromId = m_id;
		message.m_type = SCAN_ID_READ_RESPONSE;

		// Send back.
		message.calculateAndSetCrc();
		transmit(readFromLine, message);

		break;

	case SCAN_ID_CHECK:
		// Put my id in the content of the message.
		message.clearData();
		Utils::toByte(m_id, message.m_data);

		// Swap ids to send back message (addressed to master).
		message.m_targetId = message.m_fromId;
		message.m_fromId = m_id;
		message.m_type = SCAN_ID_CHECK_RESPONSE;

		// Send back.
		transmit(readFromLine, message);

		break;

	default:

		// Addressed to me?
		if (message.m_targetId == m_id) {
			if (m_handler->handleMessage(message)) {
				message.calculateAndSetCrc();
				transmit(readFromLine, message);
			}
		}
		// Not to me? pass it on unchanged.
		else {
			transmit(opposingLine, message);
		}
		break;
	}

}

void DataExchanger::transmit(DataStreamWriter *dsw, Message &message) {
    message.writeTo(dsw);
    dsw->flush();
}

DataExchanger::DataExchanger() :
        m_id(0),
        m_hardwareReader(NULL),
        m_hardwareWriter(NULL),
        m_softwareReader(NULL),
        m_softwareWriter(NULL),
        m_handler(NULL)
{}

void DataExchanger::setup(uint32_t id, Handler *handler, SerialEndpoint *debugEndpoint) {
    d.setup(debugEndpoint);
    m_id = id;
    m_handler = handler;
}

void DataExchanger::setupHardware(DataStreamReader *dsr, DataStreamWriter *dsw) {
    m_hardwareReader = dsr;
    m_hardwareWriter = dsw;
}

void DataExchanger::setupSoftware(DataStreamReader *dsr, DataStreamWriter *dsw) {
    m_softwareReader = dsr;
    m_softwareWriter = dsw;
}

void DataExchanger::loop() {
    Message m;
    if (m_hardwareReader && m_hardwareReader->available() >= MESSAGE_SIZE) {
        if (m.readFrom(m_hardwareReader) == -1) {
            d.println("ERROR while reading from hardware connection (comm A)");
        } else if (m_hardwareWriter && m_softwareWriter) {
            process(m, m_hardwareWriter, m_softwareWriter);
        } else {
        	d.println("WARNING package dismissed because there's no connection set up.");
        }
    }
    if (m_softwareReader && m_softwareReader->available() >= MESSAGE_SIZE) {
        if (m.readFrom(m_softwareReader) == -1) {
            d.println("ERROR while reading from software connection (comm B)");
        } else if (m_softwareWriter && m_hardwareWriter) {
            process(m, m_softwareWriter, m_hardwareWriter);
        } else {
        	d.println("WARNING package dismissed because there's no connection set up.");
        }
    }
}
