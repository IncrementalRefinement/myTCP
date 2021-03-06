#include "tcp_connection.hh"

#include <cassert>
#include <iostream>

// Dummy implementation of a TCP connection

// For Lab 4, please replace with a real implementation that passes the
// automated checks run by `make check`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

size_t TCPConnection::remaining_outbound_capacity() const {
    return _sender.stream_in().remaining_capacity();
}

size_t TCPConnection::bytes_in_flight() const {
    return _sender.bytes_in_flight();
}

size_t TCPConnection::unassembled_bytes() const {
    return _receiver.unassembled_bytes();
}

size_t TCPConnection::time_since_last_segment_received() const {
    return _time_since_last_segment_received;
}

void TCPConnection::segment_received(const TCPSegment &seg) {
    if (!_active) return;

    // check if RST is set
    if (seg.header().rst) {
        // close the connection, but not dont send the RST
        uncleanShutdown(false);
        return;
    }

    // ACKs in listen
    if (_receiver.getTCPReceiverState() == TCPReceiverState::LISTEN) {
        if (!seg.header().syn) {
            return;
        }
    }

    // 后手情况下不需要再等待
    if (seg.header().fin && (_sender.getTCPSenderState() != TCPSenderState::FIN_SENT && _sender.getTCPSenderState() != TCPSenderState::FIN_ACKED )) {
        _linger_after_streams_finish = false;
    }

    _time_since_last_segment_received = 0;

    // give the segment to TCPReceiver
    _receiver.segment_received(seg);

    // if the ack is set, tell the sender the field it cares
    if (seg.header().ack) {
        _sender.ack_received(seg.header().ackno, seg.header().win);
    }

//    if (checkAndSendSegments() == 0) {
//        if (_receiver.getTCPReceiverState() == TCPReceiverState::FIN_RECV && _linger_after_streams_finish) {
//            // if receive a FIN in TIME_WAIT, should resend the ACK
//            if (seg.header().fin) {
//                _sender.send_empty_segment();
//            }
//        } else {
//            _sender.send_empty_segment();
//        }
////        _sender.send_empty_segment();
//    }
//    checkAndSendSegments();

    // TODO: IDK why this shit work
    if (_sender.stream_in().buffer_empty() && seg.length_in_sequence_space()) {
        _sender.send_empty_segment();
    }
    checkAndSendSegments();

    // clean shutdown the connection if no need to _linger_after_streams_finish
    if (_receiver.getTCPReceiverState() == TCPReceiverState::FIN_RECV
        && _sender.getTCPSenderState() == TCPSenderState::FIN_ACKED
        && !_linger_after_streams_finish) {
        cleanShutdown();
    }
}

bool TCPConnection::active() const {
    return _active;
}

size_t TCPConnection::write(const string &data) {
    size_t res = _sender.stream_in().write(data);
    _sender.fill_window();
    checkAndSendSegments();
    return res;
}

//! \param[in] ms_since_last_tick number of milliseconds since the last call to this method
void TCPConnection::tick(const size_t ms_since_last_tick) {
    if (!_active) return;

    // 1. increase the tick _time_since_last_segment_received and tick the segment out
    _time_since_last_segment_received += ms_since_last_tick;
    _sender.tick(ms_since_last_tick);

    // 2. check for _segments_out
    checkAndSendSegments();

    // 3. if retry number exceed TCPConfig::MAX_RETX_ATTEMPTS, close the connection and send RST
    if (_sender.consecutive_retransmissions() > TCPConfig::MAX_RETX_ATTEMPTS) {
        uncleanShutdown(true);
    }


    // clean shutdown the connection if _linger_after_streams_finish is true and the time exceed
    if (_sender.getTCPSenderState() == TCPSenderState::FIN_ACKED
        && _receiver.getTCPReceiverState() == TCPReceiverState::FIN_RECV
        && _linger_after_streams_finish
        // as the tests suggests, it should be >= here, not >
        && _time_since_last_segment_received >= _cfg.rt_timeout * 10) {
        cleanShutdown();
    }
}

void TCPConnection::end_input_stream() {
    _sender.stream_in().end_input();
    // todo: should fillwindow(), if needed, send segment here
    _sender.fill_window();
    checkAndSendSegments();
}

void TCPConnection::connect() {
    _sender.fill_window();
    assert(_sender.segments_out().size() == 1);
    checkAndSendSegments();
}

size_t TCPConnection::checkAndSendSegments() {
    size_t totalWriteNum = 0;
    while(!_sender.segments_out().empty()) {
        TCPSegment currentSegment = _sender.segments_out().front();
        _sender.segments_out().pop();
        totalWriteNum += currentSegment.length_in_sequence_space();
        if (_receiver.ackno().has_value()) {
            currentSegment.header().ack = true;
            currentSegment.header().ackno = _receiver.ackno().value();
            currentSegment.header().win = _receiver.window_size();
        }
        _segments_out.push(currentSegment);
    }
    return totalWriteNum;
}

TCPConnection::~TCPConnection() {
    try {
        if (active()) {
            // send RST and close the connection
            cerr << "Warning: Unclean shutdown of TCPConnection\n";

            // Your code here: need to send a RST segment to the peer
            uncleanShutdown(true);
        }
    } catch (const exception &e) {
        std::cerr << "Exception destructing TCP FSM: " << e.what() << std::endl;
    }
}
void TCPConnection::uncleanShutdown(bool sendRST) {
    _sender.setStateToError();
    _sender.stream_in().set_error();
    _receiver.setStateToError();
    _receiver.stream_out().set_error();
    _active = false;

    while (!_segments_out.empty()) {
        _segments_out.pop();
    }

    while (!_sender.segments_out().empty()) {
        _sender.segments_out().pop();
    }

    if (sendRST) {
        TCPSegment RSTSegment;
        RSTSegment.header().rst = true;
//        RSTSegment.header().ack = true;
        _segments_out.push(RSTSegment);
    }
}

void TCPConnection::cleanShutdown() {
    // 两种可能: 1. 后手不需要等待 2. 先手发起的需要等待 timeout 结束后才算 shutdown
    _sender.setStateToError();
    _receiver.setStateToError();
    _active = false;
}
