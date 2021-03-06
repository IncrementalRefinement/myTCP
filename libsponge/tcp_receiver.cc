#include "tcp_receiver.hh"

// Dummy implementation of a TCP receiver

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

void TCPReceiver::segment_received(const TCPSegment &seg) {
    switch (_tcpReceiverState) {
        case TCPReceiverState::LISTEN:
            _checkpoint = 0;
            _isn = seg.header().seqno;
            if (seg.header().syn && !seg.header().fin) { // TODO: handle illegal header situation, eg: syn&fin are both set
                _tcpReceiverState = TCPReceiverState::SYN_RECV;
                _reassembler.push_substring(seg.payload().copy(), 0, seg.header().fin);
            }

            if (seg.header().syn && seg.header().fin) { // TODO: i think this is illegal but the test implies that i should receive and change status to FIN_RECV
                _tcpReceiverState = TCPReceiverState::FIN_RECV;
                _reassembler.push_substring(seg.payload().copy(), 0, seg.header().fin);
            }
            break;
        case TCPReceiverState::SYN_RECV: {
            _reassembler.push_substring(seg.payload().copy(), unwrap(seg.header().seqno, _isn, _checkpoint) - 1, seg.header().fin);
            _checkpoint = _reassembler.stream_out().bytes_written();
            if (_reassembler.stream_out().input_ended()) {
                _tcpReceiverState = TCPReceiverState::FIN_RECV;
            }
            break;
        }
        case TCPReceiverState::FIN_RECV: {
            break;
        }
        case TCPReceiverState::ERROR:
            break;
    }
}

optional<WrappingInt32> TCPReceiver::ackno() const {
    switch (_tcpReceiverState) {
        case TCPReceiverState::LISTEN :
            return {};
        case TCPReceiverState::SYN_RECV:
            return wrap(_reassembler.stream_out().bytes_written() + 1, _isn);
        case TCPReceiverState::FIN_RECV:
            return wrap(_reassembler.stream_out().bytes_written() + 2, _isn);
        default:
            return {};
    }
}

size_t TCPReceiver::window_size() const {
    return _capacity + _reassembler.stream_out().bytes_read() - _reassembler.stream_out().bytes_written();
}
