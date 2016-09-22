package mongoreplay

import (
	"fmt"
	"io"

	"github.com/10gen/llmgo"
)

// ErrNotMsg is returned if a provided buffer is too small to contain a Mongo message
var ErrNotMsg = fmt.Errorf("buffer is too small to be a Mongo message")

// OpMetadata stores metadata for an Op
type OpMetadata struct {
	// Op represents the actual operation being performed accounting for write
	// commands, so this may be "insert" or "update" even when the wire protocol
	// message was OP_QUERY.
	Op string

	// Namespace against which the operation executes.
	// If not applicable, will be blank.
	Ns string

	// Command name is the name of the command, when Op is "command" (otherwise
	// will be blank.) For example, this might be "getLastError" or
	// "serverStatus".
	Command string

	// Data contains the payload of the operation.
	// For queries: the query selector, limit and sort, etc.
	// For inserts: the document(s) to be inserted.
	// For updates: the query selector, modifiers, and upsert/multi flags.
	// For removes: the query selector for removes.
	// For commands: the full set of parameters for the command.
	// For killcursors: the list of cursorID's to be killed.
	// For getmores: the cursorID for the getmore batch.
	Data interface{}
}

// Op is a Mongo operation
type Op interface {
	// OpCode returns the OpCode for a particular kind of op.
	OpCode() OpCode

	// FromReader extracts data from a serialized op into its concrete
	// structure.
	FromReader(io.Reader) error

	// Execute performs the op on a given session, yielding the reply when
	// successful (and an error otherwise).
	Execute(*mgo.Session) (Replyable, error)

	// Meta returns metadata about the operation, useful for analysis of traffic.
	Meta() OpMetadata

	// Abbreviated returns a serialization of the op, abbreviated so it doesn't
	// exceed the given number of characters.
	Abbreviated(int) string
}

// cursorsRewriteable is an interface for any operation that has cursors
// that should be rewritten during live traffic playback.
type cursorsRewriteable interface {
	getCursorIDs() ([]int64, error)
	setCursorIDs([]int64) error
}

// Replyable is an interface representing any operation that has the
// functionality of a reply from a mongodb server. This includes both
// ReplyOps and CommandOpReplies.
type Replyable interface {
	getCursorID() (int64, error)
	Meta() OpMetadata
	getLatencyMicros() int64
	getNumReturned() int
	getErrors() []error
}

// ErrUnknownOpcode is an error that represents an unrecognized opcode.
type ErrUnknownOpcode int

func (e ErrUnknownOpcode) Error() string {
	return fmt.Sprintf("Unknown opcode %d", e)
}

// IsDriverOp checks if an operation is one of the types generated by the driver
// such as 'ismaster', or 'getnonce'. It takes an Op that has already been
// unmarshalled using its 'FromReader' method and checks if it is a command
// matching the ones the driver generates.
func IsDriverOp(op Op) bool {
	var commandType string
	var opType string
	switch castOp := op.(type) {
	case *QueryOp:
		opType, commandType = extractOpType(castOp.QueryOp.Query)
		if opType != "command" {
			return false
		}
	case *CommandOp:
		commandType = castOp.CommandName
	default:
		return false
	}

	switch commandType {
	case "isMaster", "ismaster":
		return true
	case "getnonce":
		return true
	case "ping":
		return true
	case "saslStart":
		return true
	case "saslContinue":
		return true
	default:
		return false
	}
}
