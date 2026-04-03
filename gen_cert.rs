use rcgen::generate_simple_self_signed;

fn main() {
    let cert = generate_simple_self_signed(vec!["localhost".into()]).unwrap();
    let cert_der = cert.cert.der();
    let key_der = cert.key_pair.serialize_der();

    println!("static CERT_DER: &[u8] = &{:?};", cert_der);
    println!("static KEY_DER: &[u8] = &{:?};", key_der);
}
