/****************************************************************************
** Meta object code from reading C++ file 'glwidget.h'
**
** Created by: The Qt Meta Object Compiler version 68 (Qt 6.4.2)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../../../../../libavogadro/src/glwidget.h"
#include <QtGui/qtextcursor.h>
#include <QtCore/qmetatype.h>
#include <QtCore/QList>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'glwidget.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 68
#error "This file was generated using the moc from 6.4.2. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

#ifndef Q_CONSTINIT
#define Q_CONSTINIT
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
namespace {
struct qt_meta_stringdata_Avogadro__GLWidget_t {
    uint offsetsAndSizes[112];
    char stringdata0[19];
    char stringdata1[11];
    char stringdata2[1];
    char stringdata3[13];
    char stringdata4[6];
    char stringdata5[13];
    char stringdata6[10];
    char stringdata7[17];
    char stringdata8[6];
    char stringdata9[13];
    char stringdata10[16];
    char stringdata11[10];
    char stringdata12[12];
    char stringdata13[12];
    char stringdata14[8];
    char stringdata15[7];
    char stringdata16[14];
    char stringdata17[8];
    char stringdata18[23];
    char stringdata19[17];
    char stringdata20[10];
    char stringdata21[10];
    char stringdata22[26];
    char stringdata23[8];
    char stringdata24[19];
    char stringdata25[6];
    char stringdata26[40];
    char stringdata27[7];
    char stringdata28[11];
    char stringdata29[21];
    char stringdata30[8];
    char stringdata31[6];
    char stringdata32[5];
    char stringdata33[14];
    char stringdata34[11];
    char stringdata35[12];
    char stringdata36[9];
    char stringdata37[18];
    char stringdata38[11];
    char stringdata39[13];
    char stringdata40[6];
    char stringdata41[13];
    char stringdata42[6];
    char stringdata43[10];
    char stringdata44[13];
    char stringdata45[19];
    char stringdata46[14];
    char stringdata47[14];
    char stringdata48[15];
    char stringdata49[15];
    char stringdata50[15];
    char stringdata51[15];
    char stringdata52[8];
    char stringdata53[6];
    char stringdata54[15];
    char stringdata55[7];
};
#define QT_MOC_LITERAL(ofs, len) \
    uint(sizeof(qt_meta_stringdata_Avogadro__GLWidget_t::offsetsAndSizes) + ofs), len 
Q_CONSTINIT static const qt_meta_stringdata_Avogadro__GLWidget_t qt_meta_stringdata_Avogadro__GLWidget = {
    {
        QT_MOC_LITERAL(0, 18),  // "Avogadro::GLWidget"
        QT_MOC_LITERAL(19, 10),  // "mousePress"
        QT_MOC_LITERAL(30, 0),  // ""
        QT_MOC_LITERAL(31, 12),  // "QMouseEvent*"
        QT_MOC_LITERAL(44, 5),  // "event"
        QT_MOC_LITERAL(50, 12),  // "mouseRelease"
        QT_MOC_LITERAL(63, 9),  // "mouseMove"
        QT_MOC_LITERAL(73, 16),  // "mouseDoubleClick"
        QT_MOC_LITERAL(90, 5),  // "wheel"
        QT_MOC_LITERAL(96, 12),  // "QWheelEvent*"
        QT_MOC_LITERAL(109, 15),  // "moleculeChanged"
        QT_MOC_LITERAL(125, 9),  // "Molecule*"
        QT_MOC_LITERAL(135, 11),  // "newMolecule"
        QT_MOC_LITERAL(147, 11),  // "engineAdded"
        QT_MOC_LITERAL(159, 7),  // "Engine*"
        QT_MOC_LITERAL(167, 6),  // "engine"
        QT_MOC_LITERAL(174, 13),  // "engineRemoved"
        QT_MOC_LITERAL(188, 7),  // "resized"
        QT_MOC_LITERAL(196, 22),  // "namedSelectionsChanged"
        QT_MOC_LITERAL(219, 16),  // "selectionChanged"
        QT_MOC_LITERAL(236, 9),  // "activated"
        QT_MOC_LITERAL(246, 9),  // "GLWidget*"
        QT_MOC_LITERAL(256, 25),  // "unitCellAxesRenderChanged"
        QT_MOC_LITERAL(282, 7),  // "enabled"
        QT_MOC_LITERAL(290, 18),  // "builderResultReady"
        QT_MOC_LITERAL(309, 5),  // "jobId"
        QT_MOC_LITERAL(315, 39),  // "Avogadro::rendering::Tessella..."
        QT_MOC_LITERAL(355, 6),  // "result"
        QT_MOC_LITERAL(362, 10),  // "generation"
        QT_MOC_LITERAL(373, 20),  // "sceneContentsChanged"
        QT_MOC_LITERAL(394, 7),  // "setTool"
        QT_MOC_LITERAL(402, 5),  // "Tool*"
        QT_MOC_LITERAL(408, 4),  // "tool"
        QT_MOC_LITERAL(413, 13),  // "setBackground"
        QT_MOC_LITERAL(427, 10),  // "background"
        QT_MOC_LITERAL(438, 11),  // "setMolecule"
        QT_MOC_LITERAL(450, 8),  // "molecule"
        QT_MOC_LITERAL(459, 17),  // "unselectPrimitive"
        QT_MOC_LITERAL(477, 10),  // "Primitive*"
        QT_MOC_LITERAL(488, 12),  // "unselectAtom"
        QT_MOC_LITERAL(501, 5),  // "Atom*"
        QT_MOC_LITERAL(507, 12),  // "unselectBond"
        QT_MOC_LITERAL(520, 5),  // "Bond*"
        QT_MOC_LITERAL(526, 9),  // "addEngine"
        QT_MOC_LITERAL(536, 12),  // "removeEngine"
        QT_MOC_LITERAL(549, 18),  // "loadDefaultEngines"
        QT_MOC_LITERAL(568, 13),  // "reloadEngines"
        QT_MOC_LITERAL(582, 13),  // "invalidateDLs"
        QT_MOC_LITERAL(596, 14),  // "updateGeometry"
        QT_MOC_LITERAL(611, 14),  // "toolsDestroyed"
        QT_MOC_LITERAL(626, 14),  // "updateViewport"
        QT_MOC_LITERAL(641, 14),  // "addTextOverlay"
        QT_MOC_LITERAL(656, 7),  // "QLabel*"
        QT_MOC_LITERAL(664, 5),  // "label"
        QT_MOC_LITERAL(670, 14),  // "QList<QLabel*>"
        QT_MOC_LITERAL(685, 6)   // "labels"
    },
    "Avogadro::GLWidget",
    "mousePress",
    "",
    "QMouseEvent*",
    "event",
    "mouseRelease",
    "mouseMove",
    "mouseDoubleClick",
    "wheel",
    "QWheelEvent*",
    "moleculeChanged",
    "Molecule*",
    "newMolecule",
    "engineAdded",
    "Engine*",
    "engine",
    "engineRemoved",
    "resized",
    "namedSelectionsChanged",
    "selectionChanged",
    "activated",
    "GLWidget*",
    "unitCellAxesRenderChanged",
    "enabled",
    "builderResultReady",
    "jobId",
    "Avogadro::rendering::TessellationResult",
    "result",
    "generation",
    "sceneContentsChanged",
    "setTool",
    "Tool*",
    "tool",
    "setBackground",
    "background",
    "setMolecule",
    "molecule",
    "unselectPrimitive",
    "Primitive*",
    "unselectAtom",
    "Atom*",
    "unselectBond",
    "Bond*",
    "addEngine",
    "removeEngine",
    "loadDefaultEngines",
    "reloadEngines",
    "invalidateDLs",
    "updateGeometry",
    "toolsDestroyed",
    "updateViewport",
    "addTextOverlay",
    "QLabel*",
    "label",
    "QList<QLabel*>",
    "labels"
};
#undef QT_MOC_LITERAL
} // unnamed namespace

Q_CONSTINIT static const uint qt_meta_data_Avogadro__GLWidget[] = {

 // content:
      10,       // revision
       0,       // classname
       0,    0, // classinfo
      31,   14, // methods
       1,  277, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
      13,       // signalCount

 // signals: name, argc, parameters, tag, flags, initial metatype offsets
       1,    1,  200,    2, 0x06,    2 /* Public */,
       5,    1,  203,    2, 0x06,    4 /* Public */,
       6,    1,  206,    2, 0x06,    6 /* Public */,
       7,    1,  209,    2, 0x06,    8 /* Public */,
       8,    1,  212,    2, 0x06,   10 /* Public */,
      10,    1,  215,    2, 0x06,   12 /* Public */,
      13,    1,  218,    2, 0x06,   14 /* Public */,
      16,    1,  221,    2, 0x06,   16 /* Public */,
      17,    0,  224,    2, 0x06,   18 /* Public */,
      18,    0,  225,    2, 0x06,   19 /* Public */,
      19,    0,  226,    2, 0x06,   20 /* Public */,
      20,    1,  227,    2, 0x06,   21 /* Public */,
      22,    1,  230,    2, 0x06,   23 /* Public */,

 // slots: name, argc, parameters, tag, flags, initial metatype offsets
      24,    3,  233,    2, 0x08,   25 /* Private */,
      29,    0,  240,    2, 0x08,   29 /* Private */,
      30,    1,  241,    2, 0x0a,   30 /* Public */,
      33,    1,  244,    2, 0x0a,   32 /* Public */,
      35,    1,  247,    2, 0x0a,   34 /* Public */,
      37,    1,  250,    2, 0x0a,   36 /* Public */,
      39,    1,  253,    2, 0x0a,   38 /* Public */,
      41,    1,  256,    2, 0x0a,   40 /* Public */,
      43,    1,  259,    2, 0x0a,   42 /* Public */,
      44,    1,  262,    2, 0x0a,   44 /* Public */,
      45,    0,  265,    2, 0x0a,   46 /* Public */,
      46,    0,  266,    2, 0x0a,   47 /* Public */,
      47,    0,  267,    2, 0x0a,   48 /* Public */,
      48,    0,  268,    2, 0x0a,   49 /* Public */,
      49,    0,  269,    2, 0x0a,   50 /* Public */,
      50,    0,  270,    2, 0x0a,   51 /* Public */,
      51,    1,  271,    2, 0x0a,   52 /* Public */,
      51,    1,  274,    2, 0x0a,   54 /* Public */,

 // signals: parameters
    QMetaType::Void, 0x80000000 | 3,    4,
    QMetaType::Void, 0x80000000 | 3,    4,
    QMetaType::Void, 0x80000000 | 3,    4,
    QMetaType::Void, 0x80000000 | 3,    4,
    QMetaType::Void, 0x80000000 | 9,    4,
    QMetaType::Void, 0x80000000 | 11,   12,
    QMetaType::Void, 0x80000000 | 14,   15,
    QMetaType::Void, 0x80000000 | 14,   15,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, 0x80000000 | 21,    2,
    QMetaType::Void, QMetaType::Bool,   23,

 // slots: parameters
    QMetaType::Void, QMetaType::ULong, 0x80000000 | 26, QMetaType::UInt,   25,   27,   28,
    QMetaType::Void,
    QMetaType::Void, 0x80000000 | 31,   32,
    QMetaType::Void, QMetaType::QColor,   34,
    QMetaType::Void, 0x80000000 | 11,   36,
    QMetaType::Void, 0x80000000 | 38,    2,
    QMetaType::Void, 0x80000000 | 40,    2,
    QMetaType::Void, 0x80000000 | 42,    2,
    QMetaType::Void, 0x80000000 | 14,   15,
    QMetaType::Void, 0x80000000 | 14,   15,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, 0x80000000 | 52,   53,
    QMetaType::Void, 0x80000000 | 54,   55,

 // properties: name, type, flags
      34, QMetaType::QColor, 0x00015103, uint(-1), 0,

       0        // eod
};

Q_CONSTINIT const QMetaObject Avogadro::GLWidget::staticMetaObject = { {
    QMetaObject::SuperData::link<QOpenGLWidget::staticMetaObject>(),
    qt_meta_stringdata_Avogadro__GLWidget.offsetsAndSizes,
    qt_meta_data_Avogadro__GLWidget,
    qt_static_metacall,
    nullptr,
    qt_incomplete_metaTypeArray<qt_meta_stringdata_Avogadro__GLWidget_t,
        // property 'background'
        QtPrivate::TypeAndForceComplete<QColor, std::true_type>,
        // Q_OBJECT / Q_GADGET
        QtPrivate::TypeAndForceComplete<GLWidget, std::true_type>,
        // method 'mousePress'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<QMouseEvent *, std::false_type>,
        // method 'mouseRelease'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<QMouseEvent *, std::false_type>,
        // method 'mouseMove'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<QMouseEvent *, std::false_type>,
        // method 'mouseDoubleClick'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<QMouseEvent *, std::false_type>,
        // method 'wheel'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<QWheelEvent *, std::false_type>,
        // method 'moleculeChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<Molecule *, std::false_type>,
        // method 'engineAdded'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<Engine *, std::false_type>,
        // method 'engineRemoved'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<Engine *, std::false_type>,
        // method 'resized'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'namedSelectionsChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'selectionChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'activated'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<GLWidget *, std::false_type>,
        // method 'unitCellAxesRenderChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        // method 'builderResultReady'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<unsigned long, std::false_type>,
        QtPrivate::TypeAndForceComplete<const Avogadro::rendering::TessellationResult &, std::false_type>,
        QtPrivate::TypeAndForceComplete<unsigned int, std::false_type>,
        // method 'sceneContentsChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'setTool'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<Tool *, std::false_type>,
        // method 'setBackground'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QColor &, std::false_type>,
        // method 'setMolecule'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<Molecule *, std::false_type>,
        // method 'unselectPrimitive'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<Primitive *, std::false_type>,
        // method 'unselectAtom'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<Atom *, std::false_type>,
        // method 'unselectBond'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<Bond *, std::false_type>,
        // method 'addEngine'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<Engine *, std::false_type>,
        // method 'removeEngine'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<Engine *, std::false_type>,
        // method 'loadDefaultEngines'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'reloadEngines'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'invalidateDLs'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'updateGeometry'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'toolsDestroyed'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'updateViewport'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'addTextOverlay'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<QLabel *, std::false_type>,
        // method 'addTextOverlay'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QList<QLabel*> &, std::false_type>
    >,
    nullptr
} };

void Avogadro::GLWidget::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<GLWidget *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->mousePress((*reinterpret_cast< std::add_pointer_t<QMouseEvent*>>(_a[1]))); break;
        case 1: _t->mouseRelease((*reinterpret_cast< std::add_pointer_t<QMouseEvent*>>(_a[1]))); break;
        case 2: _t->mouseMove((*reinterpret_cast< std::add_pointer_t<QMouseEvent*>>(_a[1]))); break;
        case 3: _t->mouseDoubleClick((*reinterpret_cast< std::add_pointer_t<QMouseEvent*>>(_a[1]))); break;
        case 4: _t->wheel((*reinterpret_cast< std::add_pointer_t<QWheelEvent*>>(_a[1]))); break;
        case 5: _t->moleculeChanged((*reinterpret_cast< std::add_pointer_t<Molecule*>>(_a[1]))); break;
        case 6: _t->engineAdded((*reinterpret_cast< std::add_pointer_t<Engine*>>(_a[1]))); break;
        case 7: _t->engineRemoved((*reinterpret_cast< std::add_pointer_t<Engine*>>(_a[1]))); break;
        case 8: _t->resized(); break;
        case 9: _t->namedSelectionsChanged(); break;
        case 10: _t->selectionChanged(); break;
        case 11: _t->activated((*reinterpret_cast< std::add_pointer_t<GLWidget*>>(_a[1]))); break;
        case 12: _t->unitCellAxesRenderChanged((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1]))); break;
        case 13: _t->builderResultReady((*reinterpret_cast< std::add_pointer_t<ulong>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<Avogadro::rendering::TessellationResult>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<uint>>(_a[3]))); break;
        case 14: _t->sceneContentsChanged(); break;
        case 15: _t->setTool((*reinterpret_cast< std::add_pointer_t<Tool*>>(_a[1]))); break;
        case 16: _t->setBackground((*reinterpret_cast< std::add_pointer_t<QColor>>(_a[1]))); break;
        case 17: _t->setMolecule((*reinterpret_cast< std::add_pointer_t<Molecule*>>(_a[1]))); break;
        case 18: _t->unselectPrimitive((*reinterpret_cast< std::add_pointer_t<Primitive*>>(_a[1]))); break;
        case 19: _t->unselectAtom((*reinterpret_cast< std::add_pointer_t<Atom*>>(_a[1]))); break;
        case 20: _t->unselectBond((*reinterpret_cast< std::add_pointer_t<Bond*>>(_a[1]))); break;
        case 21: _t->addEngine((*reinterpret_cast< std::add_pointer_t<Engine*>>(_a[1]))); break;
        case 22: _t->removeEngine((*reinterpret_cast< std::add_pointer_t<Engine*>>(_a[1]))); break;
        case 23: _t->loadDefaultEngines(); break;
        case 24: _t->reloadEngines(); break;
        case 25: _t->invalidateDLs(); break;
        case 26: _t->updateGeometry(); break;
        case 27: _t->toolsDestroyed(); break;
        case 28: _t->updateViewport(); break;
        case 29: _t->addTextOverlay((*reinterpret_cast< std::add_pointer_t<QLabel*>>(_a[1]))); break;
        case 30: _t->addTextOverlay((*reinterpret_cast< std::add_pointer_t<QList<QLabel*>>>(_a[1]))); break;
        default: ;
        }
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        switch (_id) {
        default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
        case 11:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< GLWidget* >(); break;
            }
            break;
        case 29:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QLabel* >(); break;
            }
            break;
        case 30:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QList<QLabel*> >(); break;
            }
            break;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (GLWidget::*)(QMouseEvent * );
            if (_t _q_method = &GLWidget::mousePress; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 0;
                return;
            }
        }
        {
            using _t = void (GLWidget::*)(QMouseEvent * );
            if (_t _q_method = &GLWidget::mouseRelease; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 1;
                return;
            }
        }
        {
            using _t = void (GLWidget::*)(QMouseEvent * );
            if (_t _q_method = &GLWidget::mouseMove; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 2;
                return;
            }
        }
        {
            using _t = void (GLWidget::*)(QMouseEvent * );
            if (_t _q_method = &GLWidget::mouseDoubleClick; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 3;
                return;
            }
        }
        {
            using _t = void (GLWidget::*)(QWheelEvent * );
            if (_t _q_method = &GLWidget::wheel; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 4;
                return;
            }
        }
        {
            using _t = void (GLWidget::*)(Molecule * );
            if (_t _q_method = &GLWidget::moleculeChanged; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 5;
                return;
            }
        }
        {
            using _t = void (GLWidget::*)(Engine * );
            if (_t _q_method = &GLWidget::engineAdded; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 6;
                return;
            }
        }
        {
            using _t = void (GLWidget::*)(Engine * );
            if (_t _q_method = &GLWidget::engineRemoved; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 7;
                return;
            }
        }
        {
            using _t = void (GLWidget::*)();
            if (_t _q_method = &GLWidget::resized; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 8;
                return;
            }
        }
        {
            using _t = void (GLWidget::*)();
            if (_t _q_method = &GLWidget::namedSelectionsChanged; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 9;
                return;
            }
        }
        {
            using _t = void (GLWidget::*)();
            if (_t _q_method = &GLWidget::selectionChanged; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 10;
                return;
            }
        }
        {
            using _t = void (GLWidget::*)(GLWidget * );
            if (_t _q_method = &GLWidget::activated; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 11;
                return;
            }
        }
        {
            using _t = void (GLWidget::*)(bool );
            if (_t _q_method = &GLWidget::unitCellAxesRenderChanged; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 12;
                return;
            }
        }
    }else if (_c == QMetaObject::ReadProperty) {
        auto *_t = static_cast<GLWidget *>(_o);
        (void)_t;
        void *_v = _a[0];
        switch (_id) {
        case 0: *reinterpret_cast< QColor*>(_v) = _t->background(); break;
        default: break;
        }
    } else if (_c == QMetaObject::WriteProperty) {
        auto *_t = static_cast<GLWidget *>(_o);
        (void)_t;
        void *_v = _a[0];
        switch (_id) {
        case 0: _t->setBackground(*reinterpret_cast< QColor*>(_v)); break;
        default: break;
        }
    } else if (_c == QMetaObject::ResetProperty) {
    } else if (_c == QMetaObject::BindableProperty) {
    }
}

const QMetaObject *Avogadro::GLWidget::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *Avogadro::GLWidget::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_Avogadro__GLWidget.stringdata0))
        return static_cast<void*>(this);
    return QOpenGLWidget::qt_metacast(_clname);
}

int Avogadro::GLWidget::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QOpenGLWidget::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 31)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 31;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 31)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 31;
    }else if (_c == QMetaObject::ReadProperty || _c == QMetaObject::WriteProperty
            || _c == QMetaObject::ResetProperty || _c == QMetaObject::BindableProperty
            || _c == QMetaObject::RegisterPropertyMetaType) {
        qt_static_metacall(this, _c, _id, _a);
        _id -= 1;
    }
    return _id;
}

// SIGNAL 0
void Avogadro::GLWidget::mousePress(QMouseEvent * _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}

// SIGNAL 1
void Avogadro::GLWidget::mouseRelease(QMouseEvent * _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 1, _a);
}

// SIGNAL 2
void Avogadro::GLWidget::mouseMove(QMouseEvent * _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 2, _a);
}

// SIGNAL 3
void Avogadro::GLWidget::mouseDoubleClick(QMouseEvent * _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 3, _a);
}

// SIGNAL 4
void Avogadro::GLWidget::wheel(QWheelEvent * _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 4, _a);
}

// SIGNAL 5
void Avogadro::GLWidget::moleculeChanged(Molecule * _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 5, _a);
}

// SIGNAL 6
void Avogadro::GLWidget::engineAdded(Engine * _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 6, _a);
}

// SIGNAL 7
void Avogadro::GLWidget::engineRemoved(Engine * _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 7, _a);
}

// SIGNAL 8
void Avogadro::GLWidget::resized()
{
    QMetaObject::activate(this, &staticMetaObject, 8, nullptr);
}

// SIGNAL 9
void Avogadro::GLWidget::namedSelectionsChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 9, nullptr);
}

// SIGNAL 10
void Avogadro::GLWidget::selectionChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 10, nullptr);
}

// SIGNAL 11
void Avogadro::GLWidget::activated(GLWidget * _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 11, _a);
}

// SIGNAL 12
void Avogadro::GLWidget::unitCellAxesRenderChanged(bool _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 12, _a);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
