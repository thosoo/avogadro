/****************************************************************************
** Meta object code from reading C++ file 'textmatrixeditor.h'
**
** Created by: The Qt Meta Object Compiler version 68 (Qt 6.4.2)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../../../../../libavogadro/src/textmatrixeditor.h"
#include <QtGui/qtextcursor.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'textmatrixeditor.h' doesn't include <QObject>."
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
struct qt_meta_stringdata_Avogadro__TextMatrixEditor_t {
    uint offsetsAndSizes[22];
    char stringdata0[27];
    char stringdata1[10];
    char stringdata2[1];
    char stringdata3[8];
    char stringdata4[10];
    char stringdata5[16];
    char stringdata6[4];
    char stringdata7[12];
    char stringdata8[9];
    char stringdata9[14];
    char stringdata10[12];
};
#define QT_MOC_LITERAL(ofs, len) \
    uint(sizeof(qt_meta_stringdata_Avogadro__TextMatrixEditor_t::offsetsAndSizes) + ofs), len 
Q_CONSTINIT static const qt_meta_stringdata_Avogadro__TextMatrixEditor_t qt_meta_stringdata_Avogadro__TextMatrixEditor = {
    {
        QT_MOC_LITERAL(0, 26),  // "Avogadro::TextMatrixEditor"
        QT_MOC_LITERAL(27, 9),  // "isInvalid"
        QT_MOC_LITERAL(37, 0),  // ""
        QT_MOC_LITERAL(38, 7),  // "isValid"
        QT_MOC_LITERAL(46, 9),  // "setMatrix"
        QT_MOC_LITERAL(56, 15),  // "Eigen::Matrix3d"
        QT_MOC_LITERAL(72, 3),  // "mat"
        QT_MOC_LITERAL(76, 11),  // "resetMatrix"
        QT_MOC_LITERAL(88, 8),  // "validate"
        QT_MOC_LITERAL(97, 13),  // "markAsInvalid"
        QT_MOC_LITERAL(111, 11)   // "markAsValid"
    },
    "Avogadro::TextMatrixEditor",
    "isInvalid",
    "",
    "isValid",
    "setMatrix",
    "Eigen::Matrix3d",
    "mat",
    "resetMatrix",
    "validate",
    "markAsInvalid",
    "markAsValid"
};
#undef QT_MOC_LITERAL
} // unnamed namespace

Q_CONSTINIT static const uint qt_meta_data_Avogadro__TextMatrixEditor[] = {

 // content:
      10,       // revision
       0,       // classname
       0,    0, // classinfo
       7,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       2,       // signalCount

 // signals: name, argc, parameters, tag, flags, initial metatype offsets
       1,    0,   56,    2, 0x06,    1 /* Public */,
       3,    0,   57,    2, 0x06,    2 /* Public */,

 // slots: name, argc, parameters, tag, flags, initial metatype offsets
       4,    1,   58,    2, 0x0a,    3 /* Public */,
       7,    0,   61,    2, 0x0a,    5 /* Public */,
       8,    0,   62,    2, 0x09,    6 /* Protected */,
       9,    0,   63,    2, 0x09,    7 /* Protected */,
      10,    0,   64,    2, 0x09,    8 /* Protected */,

 // signals: parameters
    QMetaType::Void,
    QMetaType::Void,

 // slots: parameters
    QMetaType::Void, 0x80000000 | 5,    6,
    QMetaType::Void,
    QMetaType::Bool,
    QMetaType::Void,
    QMetaType::Void,

       0        // eod
};

Q_CONSTINIT const QMetaObject Avogadro::TextMatrixEditor::staticMetaObject = { {
    QMetaObject::SuperData::link<QTextEdit::staticMetaObject>(),
    qt_meta_stringdata_Avogadro__TextMatrixEditor.offsetsAndSizes,
    qt_meta_data_Avogadro__TextMatrixEditor,
    qt_static_metacall,
    nullptr,
    qt_incomplete_metaTypeArray<qt_meta_stringdata_Avogadro__TextMatrixEditor_t,
        // Q_OBJECT / Q_GADGET
        QtPrivate::TypeAndForceComplete<TextMatrixEditor, std::true_type>,
        // method 'isInvalid'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'isValid'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'setMatrix'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const Eigen::Matrix3d &, std::false_type>,
        // method 'resetMatrix'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'validate'
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        // method 'markAsInvalid'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'markAsValid'
        QtPrivate::TypeAndForceComplete<void, std::false_type>
    >,
    nullptr
} };

void Avogadro::TextMatrixEditor::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<TextMatrixEditor *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->isInvalid(); break;
        case 1: _t->isValid(); break;
        case 2: _t->setMatrix((*reinterpret_cast< std::add_pointer_t<Eigen::Matrix3d>>(_a[1]))); break;
        case 3: _t->resetMatrix(); break;
        case 4: { bool _r = _t->validate();
            if (_a[0]) *reinterpret_cast< bool*>(_a[0]) = std::move(_r); }  break;
        case 5: _t->markAsInvalid(); break;
        case 6: _t->markAsValid(); break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (TextMatrixEditor::*)();
            if (_t _q_method = &TextMatrixEditor::isInvalid; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 0;
                return;
            }
        }
        {
            using _t = void (TextMatrixEditor::*)();
            if (_t _q_method = &TextMatrixEditor::isValid; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 1;
                return;
            }
        }
    }
}

const QMetaObject *Avogadro::TextMatrixEditor::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *Avogadro::TextMatrixEditor::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_Avogadro__TextMatrixEditor.stringdata0))
        return static_cast<void*>(this);
    return QTextEdit::qt_metacast(_clname);
}

int Avogadro::TextMatrixEditor::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QTextEdit::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 7)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 7;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 7)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 7;
    }
    return _id;
}

// SIGNAL 0
void Avogadro::TextMatrixEditor::isInvalid()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}

// SIGNAL 1
void Avogadro::TextMatrixEditor::isValid()
{
    QMetaObject::activate(this, &staticMetaObject, 1, nullptr);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
