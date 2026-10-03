// Bibliotecas para entrada/salida, tabla de transiciones, valores opcionales y listas.
#include <iostream>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
using namespace std;

// Un token guarda la categoria y el texto reconocido.
// Ejemplo: tipo = "REGISTRO", textoToken = "AL".
struct Token { string tipo, textoToken; };
// Organiza los datos de una instruccion que ya paso la validacion.
struct Instruccion {
    string operacion;
    vector<string> registros;
    // Texto decimal para admitir direcciones sin limite de entero nativo.
    // nullopt indica que ADD y END no tienen direccion.
    optional<string> direccion;
};

// AFD: revisa la instruccion caracter por caracter y comprueba su orden.
class AFD {
    // Cada estado indica que parte de la instruccion se ha reconocido.
    enum Estado {
        Inicio, M, MO, MOV, EspMOV, RegMOV, FinRegMOV, ComaMOV,
        AntesComaMOV, NumMOV, A, AD, ADD, EspADD, AL_A, AL_ADD,
        AntesComaADD, ComaADD, BL_B, BL_ADD, S, ST, STO, EspSTO,
        NumSTO, E, EN, END
    };
    // (estado actual, caracter leido) -> siguiente estado.
    map<pair<Estado, char>, Estado> tabla;
    // Registra una regla; por ejemplo, t(Inicio,'M',M).
    void t(Estado a, char c, Estado b) { tabla[{a,c}] = b; }
public:
    AFD() {
        // MOV: exige un espacio, AL o BL, una coma y una direccion.
        t(Inicio,'M',M); t(M,'O',MO); t(MO,'V',MOV);
        t(MOV,' ',EspMOV); t(EspMOV,' ',EspMOV);
        t(EspMOV,'A',RegMOV); t(EspMOV,'B',RegMOV);
        t(RegMOV,'L',FinRegMOV);
        // Se permiten espacios antes y despues de la coma.
        t(FinRegMOV,' ',AntesComaMOV); t(AntesComaMOV,' ',AntesComaMOV);
        t(FinRegMOV,',',ComaMOV); t(AntesComaMOV,',',ComaMOV);
        t(ComaMOV,' ',ComaMOV);
        // ADD: solo permite AL como primer registro y BL como segundo.
        t(Inicio,'A',A); t(A,'D',AD); t(AD,'D',ADD);
        t(ADD,' ',EspADD); t(EspADD,' ',EspADD);
        t(EspADD,'A',AL_A); t(AL_A,'L',AL_ADD);
        t(AL_ADD,' ',AntesComaADD); t(AntesComaADD,' ',AntesComaADD);
        t(AL_ADD,',',ComaADD); t(AntesComaADD,',',ComaADD);
        t(ComaADD,' ',ComaADD); t(ComaADD,'B',BL_B); t(BL_B,'L',BL_ADD);
        // STO: reconoce la palabra y exige espacio antes de la direccion.
        t(Inicio,'S',S); t(S,'T',ST); t(ST,'O',STO);
        t(STO,' ',EspSTO); t(EspSTO,' ',EspSTO);
        // END: no recibe operandos.
        t(Inicio,'E',E); t(E,'N',EN); t(EN,'D',END);
        // Una direccion tiene uno o mas digitos; no admite signo ni punto.
        for (char c='0'; c<='9'; ++c) {
            t(ComaMOV,c,NumMOV); t(NumMOV,c,NumMOV);
            t(EspSTO,c,NumSTO); t(NumSTO,c,NumSTO);
        }
    }
    // Devuelve tokens si toda la entrada es valida; si falla, lanza un error.
    vector<Token> validar(const string& texto) const {
        Estado estado=Inicio;
        vector<Token> tokens;
        string textoToken; // Acumula letras o digitos hasta llegar a un separador.
        // Funcion local: clasifica el texto acumulado, lo guarda y lo vacia.
        // [&] permite usar las variables locales textoToken y tokens.
        auto guardar = [&]() {
            if (textoToken.empty()) return;
            string tipo = (textoToken=="AL" || textoToken=="BL") ? "REGISTRO" :
                          (textoToken[0]>='0' && textoToken[0]<='9') ? "NUMERO" : textoToken;
            tokens.push_back({tipo,textoToken});
            textoToken.clear();
        };
        // Avanzamos una posicion a la vez, sin saltarnos ningun caracter.
        for (size_t i=0; i<texto.size(); ++i) {
            char c=texto[i];
            // Buscamos si este caracter esta permitido en el estado actual.
            auto it=tabla.find({estado,c});
            // No existe transicion: se rechaza y main recibe la excepcion.
            if (it==tabla.end()) {
                string causa="caracter u operando no permitido";
                if (estado==FinRegMOV || estado==AntesComaMOV ||
                    estado==AL_ADD || estado==AntesComaADD) causa="se esperaba una coma";
                if (estado==END || estado==BL_ADD) causa="sobran componentes";
                throw runtime_error(causa+" en la posicion "+to_string(i+1));
            }
            estado=it->second; // Aplicamos la transicion encontrada.
            // El espacio separa tokens; la coma tambien se guarda como token.
            if (c==' ' || c==',') {
                guardar();
                if (c==',') tokens.push_back({"COMA",","});
            } else textoToken+=c;
        }
        // Estos son los estados de aceptacion. Otros indican una entrada incompleta.
        if (estado!=NumMOV && estado!=NumSTO && estado!=BL_ADD && estado!=END)
            throw runtime_error("instruccion incompleta: faltan componentes");
        guardar(); // Conserva el ultimo token aunque no termine en separador.
        return tokens;
    }
};

// Se llama solo tras validar: el primer token siempre contiene la operacion.
Instruccion construir(const vector<Token>& tokens) {
    Instruccion ins{tokens.at(0).textoToken,{},nullopt};
    for (const auto& token: tokens) {
        if (token.tipo=="REGISTRO") ins.registros.push_back(token.textoToken);
        if (token.tipo=="NUMERO") {
            // Quitamos ceros iniciales: 0006 se guarda como 6; 000 como 0.
            size_t primero=token.textoToken.find_first_not_of('0');
            ins.direccion=primero==string::npos ? "0" : token.textoToken.substr(primero);
        }
    }
    return ins;
}

// Moore recibe el objeto ya construido: no vuelve a analizar la entrada.
// Solo describe microoperaciones; no modifica registros ni memoria reales.
class Moore {
    enum Estado { DirMOV, Leer, CargarAL, CargarBL, Sumar,
                  DirSTO, CopiarACC, Escribir, Detener, Fin };
    Instruccion ins;
    Estado estado;
    // Cada estado tiene una salida. El objeto aporta la direccion como parametro.
    string salida() const {
        switch (estado) {
            case DirMOV: case DirSTO: return "MAR <- "+ins.direccion.value();
            case Leer: return "MBR <- M[MAR]";
            case CargarAL: return "AL <- MBR";
            case CargarBL: return "BL <- MBR";
            case Sumar: return "ACC <- AL + BL";
            case CopiarACC: return "MBR <- ACC";
            case Escribir: return "M[MAR] <- MBR";
            case Detener: return "HALT <- 1";
            case Fin: return "";
        }
        throw logic_error("Estado desconocido");
    }
    // Define las rutas: MOV -> direccion, lectura, carga; STO -> direccion, copia, escritura.
    Estado siguiente() const {
        switch (estado) {
            case DirMOV: return Leer;
            case Leer: return ins.registros.at(0)=="AL" ? CargarAL : CargarBL;
            case DirSTO: return CopiarACC;
            case CopiarACC: return Escribir;
            default: return Fin; // Las demas operaciones ya son el ultimo paso.
        }
    }
public:
    // Selecciona el estado inicial segun la operacion validada.
    explicit Moore(const Instruccion& objeto): ins(objeto), estado(Fin) {
        if (ins.operacion=="MOV") estado=DirMOV;
        else if (ins.operacion=="ADD") estado=Sumar;
        else if (ins.operacion=="STO") estado=DirSTO;
        else if (ins.operacion=="END") estado=Detener;
        else throw invalid_argument("Operacion desconocida");
    }
    // Fin no emite ninguna microoperacion.
    bool termino() const { return estado==Fin; }
    // Una llamada imprime una microoperacion y avanza exactamente un estado.
    void siguientePaso(ostream& out) {
        if (termino()) return;
        out << salida() << '\n';
        estado=siguiente();
    }
};

// Coordina las etapas: leer, validar, mostrar datos y generar microoperaciones.
int main() {
    cout << "ENTRADA\n";
    string texto;
    getline(cin,texto); // Lee la linea completa, incluidos sus espacios.
    cout << "VALIDACION MEDIANTE AFD\n";
    vector<Token> tokens;
    // try/catch permite mostrar el error del AFD y detener el procesamiento.
    try { tokens=AFD().validar(texto); }
    catch (const runtime_error& error) {
        cout << "Instruccion invalida: " << error.what() << ".\n"
             << "No se construye el objeto instruccion.\nNo se generan microoperaciones.\n";
        return 1; // Termina con error; no se construye el objeto ni se inicia Moore.
    }
    cout << "Instruccion valida.\n\nTOKENS RECONOCIDOS\n";
    for (const auto& t: tokens) cout << t.tipo << "(\"" << t.textoToken << "\")\n";
    // Los tokens aceptados se convierten en un objeto facil de consultar.
    Instruccion ins=construir(tokens);
    cout << "\nCOMPONENTES IDENTIFICADOS\nMnemonico: " << ins.operacion << '\n';
    for (const auto& r: ins.registros) cout << "Registro: " << r << '\n';
    // optional permite comprobar si hay direccion antes de acceder a ella.
    if (ins.direccion) cout << "Direccion de memoria: " << *ins.direccion
                            << "\nDireccionamiento: directo\n";
    // Mostramos el objeto; value_or imprime null cuando no hay direccion.
    cout << "\nOBJETO INSTRUCCION\n{\n  operacion: \"" << ins.operacion
         << "\",\n  registros: [";
    for (size_t i=0; i<ins.registros.size(); ++i) {
        if (i) cout << ", ";
        cout << '"' << ins.registros[i] << '"';
    }
    cout << "],\n  direccion: " << ins.direccion.value_or("null") << "\n}\n";
    cout << "\nMICROOPERACIONES GENERADAS POR MOORE\n";
    // Recorremos Moore hasta Fin y numeramos sus salidas desde 1.
    Moore maquina(ins);
    int paso=1;
    while (!maquina.termino()) {
        cout << paso++ << ". ";
        maquina.siguientePaso(cout);
    }
    cout << "Generacion terminada.\n";
}
