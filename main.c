// All credit to this PICKIT2 USB utility goes to Tom Schouten:
// https://github.com/zwizwa/staapl/blob/96ccc35ef2dc5711fb1659463c50151678978a6a/tools/pk2serial.c
	/* pk2serial (c) 2009 Tom Schouten

	I hereby grant the rights for any use of this software, given that
	this copyright notice is preserved.

	Connect to PK2 and start a serial console on stdio. Use this in
	conjunction with socat for pty/socket/... emulation.

	*/

// I've just written a half-crappy Python wrapper (first time trying). It seems to work OK with the test script.
// pk2serial for Python 2.7
// Written by Hans de Jong

#include <Python.h>

#include <stdio.h>
#include <usb.h>


#include <fcntl.h>
#include <errno.h>
#include <ctype.h>
#include <signal.h>
#include <sys/select.h>

#include "./pk2dev.h"

// Buffers for read() and write()
char rxBuffer[1024*16];
char txBuffer[1024*16];
int rxHead;
int txHead;


int usec_per_byte = 100;

// Signal 2 is CTRL+C in Unix, and forces closing the port.
int signal2Received = 0;

// Debugging level
int verbose = 0;

#define LOGL(level, ...)   { if (verbose >= level)  fprintf(stderr, __VA_ARGS__); }
#define LOG(...)   { LOGL(1, __VA_ARGS__); }

#define LOG_ERROR(...) { fprintf(stderr, __VA_ARGS__); }
#define RAISE_ERROR(...) { LOG_ERROR(__VA_ARGS__); exit(1); }

// PICKIT 2 USB ID's
int vendor  = 0x04d8;
int product = 0x0033;

// PICKIT2 connect settings
float voltage_output = 3.3f;
int reset_on_connect = 0;
int baudrate = 9600;

// Handle status
int handleOpen = 0;
usb_dev_handle *handle = NULL; // only one device per program instance.


// Python prototypes
static PyObject *
pk2Open(PyObject *self, PyObject *args);
static PyObject *
pk2Close(PyObject *self, PyObject *args);
static PyObject *
pk2Power(PyObject *self, PyObject *args);
static PyObject *
pk2ResetOnConnect(PyObject *self, PyObject*args);
static PyObject *
pk2Read(PyObject *self, PyObject *args);
static PyObject *
pk2Write(PyObject *self, PyObject *args);
static PyObject *
pk2IsOpen(PyObject *self, PyObject *args);

// Commands available
static PyMethodDef Pk2Cmdset[] = {
    {"open",  pk2Open, METH_VARARGS, "Open pk2 device."},
    {"close",  pk2Close, METH_VARARGS, "Close pk2 device."},
    {"power",  pk2Power, METH_VARARGS, "Set power rail output (voltage) , 0 to disable it."},
    {"resetOnConnect",  pk2ResetOnConnect, METH_VARARGS, "Reset target on open()"},
    {"read",  pk2Read, METH_VARARGS, "Read x no of bytes from PK2"},
    {"write",  pk2Write, METH_VARARGS, "Write x no of bytes to PK2"},
    {"isOpen",  pk2IsOpen, METH_VARARGS, "Check whethern PK2 port is open"},
    {NULL, NULL, 0, NULL}        /* Sentinel */
};


/***************** PICKIT2 USB SERIAL FUNCTIONS *********************/
void log_buf(char *kind, unsigned char *data, int len) {
    int i;
    if (len) {
        LOGL(2, "%s: ", kind);
        for(i=0; i<len; i++) { LOGL(2, "%02X ", data[i]); }
        LOGL(2, "\n");
    }
}

/* lowlevel transfer */
void usb_send_buf(void *buffer) {
    int rv;
    if (BUFFER_SIZE != (rv = usb_interrupt_write(handle, ENDPOINT_OUT, buffer, BUFFER_SIZE, TIMEOUT))) {
        RAISE_ERROR("Invalid size request: %d bytes.\n", rv);
    }
}
void usb_receive_buf(void *buffer) {
    int rv;
    if (BUFFER_SIZE != (rv = usb_interrupt_read(handle, ENDPOINT_IN, buffer, BUFFER_SIZE, TIMEOUT))) {
        RAISE_ERROR("Invalid size response: %d bytes.\n", rv);
    }
}

/* buffer formatting based on data length */
void usb_send(void *data, int len) {
    char buffer[BUFFER_SIZE];
    log_buf("W", data, len);
    memset(buffer, END_OF_BUFFER, BUFFER_SIZE);
    memcpy(buffer, data, len);
    usb_send_buf(buffer);
}
void usb_receive(void *data, int len) {
    char buffer[BUFFER_SIZE];
    usb_receive_buf(buffer);
    memcpy(data, buffer, len);
    log_buf("R", data, len);
}

/* one command cycle */
void usb_call(void *cmd,   int cmd_len,
              void *reply, int reply_len) {
    usb_send(cmd, cmd_len);
    if (reply_len) usb_receive(reply, reply_len); // PK2 doesn't send empty buffers
}

#define TRANSACTION(reply_type, ...) ({                                 \
            char cmd[] = { __VA_ARGS__ };                               \
            reply_type reply;                                           \
            usb_call(cmd, sizeof(cmd), &reply, sizeof(reply_type));     \
            reply; })

#define COMMAND(...) {                          \
        char cmd[] = { __VA_ARGS__ };           \
        usb_call(cmd, sizeof(cmd), NULL, 0); }

#define DEFINE_TRANSACTION(return_type, name, ...) return_type name (void) { \
        return  TRANSACTION(return_type, __VA_ARGS__); }


DEFINE_TRANSACTION(short int, get_status, GET_STATUS)

void set_vdd(float vdd) {
    float tolerance = 0.80;
    float vfault = tolerance * vdd;
    int vddi = ((32.0f * vdd) + 10.5f) * 64.0f;
    int vfi  = (vfault / 5.0) * 255.0f;
    COMMAND(SET_VDD, vddi & 0xFF, (vddi >> 8) & 0xFF, vfi);
}

void reset_pk2(void)     { COMMAND(RESET); }
void reset_hold(int rst) { COMMAND(EXECUTE_SCRIPT, 1, rst ? MCLR_GND_ON : MCLR_GND_OFF); }
void reset_release(void) { COMMAND(EXECUTE_SCRIPT, 1, MCLR_GND_OFF); }
void target_off(int pwr) { COMMAND(EXECUTE_SCRIPT, 2, VDD_OFF, pwr ? VDD_GND_ON : VDD_GND_OFF); }
void target_on(int pwr)  { COMMAND(EXECUTE_SCRIPT, 2, VDD_GND_OFF, pwr ? VDD_ON : VDD_OFF); usleep(50000); }
void uart_off(void)      { COMMAND(EXIT_UART_MODE); }
void uart_on(float rate) {
    int irate = (65536.0f - (((1.0f / rate) - .000003f) / .000000167f));
    COMMAND(CLR_UPLOAD_BFR);
    COMMAND(CLR_DOWNLOAD_BFR);
    COMMAND(ENTER_UART_MODE, irate & 0xFF, (irate >> 8) & 0xFF);
}

void dots(int n) { while (n--) LOG("."); }


/* Transfer data between Host and the PK2.  These use the lower level
   transfer functions. */
int transfer_output(void) {

    if (txHead == 0) return 0;

    char buf[BUFFER_SIZE+2];
    memset(buf, END_OF_BUFFER, BUFFER_SIZE);

    buf[0] = DOWNLOAD_DATA;
    buf[1] = txHead > BUFFER_SIZE ? BUFFER_SIZE : txHead;// read(fd, buf+2, max_tx);

    memcpy(buf+2, txBuffer, buf[1]);
    txHead -= buf[1];
    memcpy(txBuffer, txBuffer + buf[1], txHead);

    LOG("[pk2serial] Write %d bytes, head @ %d\n", buf[1], txHead);

    if (-1 == buf[1]) return buf[1];

    LOG("I %2d\n", buf[1]);
    usb_send_buf(buf);
    return buf[1];
}

void transfer_input(void) {   // from PK2 -> host
    char buf[BUFFER_SIZE];
    char cmd[] = {UPLOAD_DATA};

    usb_send(cmd, 1);
    usb_receive_buf(buf);

    if (buf[0])
    {
        LOG("[pk2serial] Read %d bytes, head @ %d\n", buf[0], rxHead+buf[0]);
        memcpy(rxBuffer + rxHead, buf + 1, buf[0]);
        rxHead += buf[0];
    }
}












/************** PYTHON MODULE *************/


PyMODINIT_FUNC
initpk2serial(void)
{
    (void) Py_InitModule("pk2serial", Pk2Cmdset);
}

void
_pk2_int_handler(int signal)
{
    // Emergency exit?
    LOG("got signal %d\n", signal);

    if (signal == 2) signal2Received = 1;
}

void _pk2_close(void)
{
    if (handleOpen != 0)
    {
        uart_off();
        reset_pk2();

        if (-1 == usb_release_interface(handle, 0)) {
            RAISE_ERROR("[pk2serial] Can't release interface.\n");
        }
        if (-1 == usb_close(handle)) {
            RAISE_ERROR("[pk2serial] Can't close device.\n");
        }
        handleOpen = 0;
        LOG("[pk2serial] Closed pickit2\n");
    }
}

void _pk2_tick(void)
{
    int lastRxHead = -1;
    int rxHeadInc = 0;

    if (signal2Received == 1)
    {
        _pk2_close();
        return;
    }
    //fprintf(stderr,"rxh: %d, txh: %d\n", rxHead, txHead);
    while (lastRxHead != rxHead ||
            txHead > 0)
    {
        lastRxHead = rxHead;
        transfer_input();

        // TX_Head > output_fd
        if (txHead > 0)
        {
           transfer_output();
        }
        rxHeadInc = rxHead - lastRxHead;
        int dl = 50 + rxHeadInc * usec_per_byte;
        usleep(dl);
    }
    usleep(5000);
}

static PyObject *
pk2Close(PyObject *self, PyObject *args)
{
    int wasOpen = handleOpen;
    _pk2_close();
    return (wasOpen) ? PyLong_FromLong(1) : PyLong_FromLong(0);
}

static PyObject *
pk2IsOpen(PyObject *self, PyObject *args)
{
    return (handleOpen) ? PyLong_FromLong(1) : PyLong_FromLong(0);
}


static PyObject *
pk2Read(PyObject *self, PyObject *args)
{
    PyObject * listRead;

    if (handleOpen == 0)
    {
        LOG("[pk2serial] Stream closed\n");
        return PyLong_FromLong(0);
    }

    _pk2_tick();

    if (rxHead > 0)
    {
        LOG("[pk2serial] Reading %d bytes\n", rxHead);
        listRead = PyString_FromString(rxBuffer);

        // Clear buffer
        memset(rxBuffer, 0, sizeof(rxBuffer));
        rxHead = 0;

        return listRead;
    }
    else
    {
        return PyLong_FromLong(0);
    }
}

static PyObject *
pk2Write(PyObject *self, PyObject *args)
{
    PyObject * strObj;

    if (handleOpen == 0)
    {
        LOG("[pk2serial] Stream closed\n");
        return PyLong_FromLong(0);
    }

    strObj = PyTuple_GetItem(args, 0);
    if (strObj == NULL)
    {
        LOG("[pk2serial] Missing argument (byte array)\n");
        return PyLong_FromLong(0);
    }
    if(PyString_Check(strObj) == 0)
    {
        LOG("[pk2serial] Need to write something\n");
        return PyLong_FromLong(0);
    }

    char* s = PyString_AsString(strObj);
    txHead = PyString_Size(strObj);
    memcpy(txBuffer, s, txHead);

    _pk2_tick();

    return PyLong_FromLong(1);
}


static PyObject *
pk2Power(PyObject *self, PyObject *args)
{
    if (handleOpen)
    {
        LOG("[pk2serial] Cannot change power when on\n");
        return PyLong_FromLong(0);
    }
    if (!PyArg_ParseTuple(args, "f", &voltage_output))
    {
        voltage_output = 0;
        LOG("[pk2serial] Could not parse power, assuming 0\n");
        return PyLong_FromLong(0);
    }
    return PyLong_FromLong(1);
}

static PyObject *
pk2ResetOnConnect(PyObject *self, PyObject *args)
{
    if (handleOpen)
    {
        // TODO: Implement
        LOG("[pk2serial] Cannot reset when on..\n");
        return PyLong_FromLong(0);
    }
    if (!PyArg_ParseTuple(args, "b", &reset_on_connect))
    {
        LOG("[pk2serial] Cannot figure out what you want to do with this reset signal...\n");
        return PyLong_FromLong(0);
    }

    return PyLong_FromLong(1);
}

static PyObject *
pk2Open(PyObject *self, PyObject *args)
{
    struct usb_bus *busses;
    struct usb_bus *bus;

    if (!PyArg_ParseTuple(args, "i", &baudrate))
    {
        fprintf(stderr,"[pk2serial] Error parsing baudrate\n");
        return PyLong_FromLong(0);
    }
    usec_per_byte = 1000000 / baudrate;

    if (handleOpen == 1)
    {
        LOG("[pk2serial] Must close handle first before opening\n");
        return PyLong_FromLong(0);
    }

    // Open USB port
    signal(SIGINT, _pk2_int_handler);

    usb_init();
    usb_find_busses();
    usb_find_devices();

    busses = usb_get_busses();


    for (bus = busses; bus && handleOpen == 0; bus = bus->next) {
        struct usb_device *dev;

        for (dev = bus->devices; dev && handleOpen == 0; dev = dev->next) {

            /* Find first PK2 */
            if ((dev->descriptor.idVendor  == vendor)
                && (dev->descriptor.idProduct == product)) {

                if (!(handle = usb_open(dev))) {
                    RAISE_ERROR("[pk2serial] Can't open device.\n");
                    return PyLong_FromLong(0);
                }
                /* FIXME: Detach if necessary
                   usb_get_driver_np
                   usb_detach_kernel_driver_np
                 */
                if (-1 == usb_set_configuration(handle, 2)) { // Try vendor config (not HID!)
                    RAISE_ERROR("[pk2serial] Can't set vendor config.\n");
                    return PyLong_FromLong(0);
                }
                if (-1 == usb_claim_interface(handle, 0)) {
                    RAISE_ERROR("[pk2serial] Can't claim interface.\n");
                    return PyLong_FromLong(0);
                }
                if (-1 == fcntl(input_fd, F_SETFL, O_NONBLOCK))  {
                    RAISE_ERROR("Can't set input_fd O_NONBLOCK.\n");
                    return PyLong_FromLong(0);
                }
                LOG("[pk2serial] Found pickit2\n");
                handleOpen = 1;
            }
        }
    }

    // Open device.
    if (handleOpen == 1)
    {
        int power_target = voltage_output > 1.0f ? 1 : 0;

        /* Setup PK2 with target off.  FIXME: connect to running target! */
        if (reset_on_connect) reset_hold(1);
        target_off(power_target);
        set_vdd(voltage_output);

        /* Start target. */
        target_on(power_target);
        reset_release();

        /* Check if voltage is ok. */
        LOG("[pk2serial] status %04X\n", get_status());

        memset(rxBuffer, 0, sizeof(rxBuffer));
        memset(txBuffer, 0, sizeof(txBuffer));

        /* Go */
        uart_on(baudrate);

        LOG("[pk2serial] Opened port\n");
    }

    return PyLong_FromLong(1);
}

int
main(int argc, char *argv[])
{
    /* Pass argv[0] to the Python interpreter */
    Py_SetProgramName(argv[0]);

    /* Initialize the Python interpreter.  Required. */
    Py_Initialize();

    /* Add a static module */
    initpk2serial();

    return 1;
}
