#![rustfmt::skip]
/// @generated rust packets from test.
use bytes::{Buf, BufMut, Bytes, BytesMut};
use std::convert::{TryFrom, TryInto};
use std::cell::Cell;
use std::fmt;
use std::sync::Arc;
use thiserror::Error;
type Result<T> = std::result::Result<T, Error>;
/// Private prevents users from creating arbitrary scalar values
/// in situations where the value needs to be validated.
/// Users can freely deref the value, but only the backend
/// may create it.
#[derive(Debug, Clone, Copy, Hash, PartialEq, Eq, PartialOrd, Ord)]
pub struct Private<T>(T);
impl<T> std::ops::Deref for Private<T> {
    type Target = T;
    fn deref(&self) -> &Self::Target {
        &self.0
    }
}
#[derive(Debug, Error)]
pub enum Error {
    #[error("Packet parsing failed")]
    InvalidPacketError,
    #[error("{field} was {value:x}, which is not known")]
    ConstraintOutOfBounds { field: String, value: u64 },
    #[error("Got {actual:x}, expected {expected:x}")]
    InvalidFixedValue { expected: u64, actual: u64 },
    #[error("when parsing {obj} needed length of {wanted} but got {got}")]
    InvalidLengthError { obj: String, wanted: usize, got: usize },
    #[error(
        "array size ({array} bytes) is not a multiple of the element size ({element} bytes)"
    )]
    InvalidArraySize { array: usize, element: usize },
    #[error("Due to size restrictions a struct could not be parsed.")]
    ImpossibleStructError,
    #[error("when parsing field {obj}.{field}, {value} is not a valid {type_} value")]
    InvalidEnumValueError { obj: String, field: String, value: u64, type_: String },
    #[error("expected child {expected}, got {actual}")]
    InvalidChildError { expected: &'static str, actual: String },
}
pub trait Packet {
    fn to_bytes(self) -> Bytes;
    fn to_vec(self) -> Vec<u8>;
}
#[repr(u64)]
#[derive(Debug, Clone, Copy, Hash, Eq, PartialEq)]
#[cfg_attr(feature = "serde", derive(serde::Serialize, serde::Deserialize))]
#[cfg_attr(feature = "serde", serde(try_from = "u8", into = "u8"))]
pub enum Enum7 {
    A = 0x1,
    B = 0x2,
}
impl TryFrom<u8> for Enum7 {
    type Error = u8;
    fn try_from(value: u8) -> std::result::Result<Self, Self::Error> {
        match value {
            0x1 => Ok(Enum7::A),
            0x2 => Ok(Enum7::B),
            _ => Err(value),
        }
    }
}
impl From<&Enum7> for u8 {
    fn from(value: &Enum7) -> Self {
        match value {
            Enum7::A => 0x1,
            Enum7::B => 0x2,
        }
    }
}
impl From<Enum7> for u8 {
    fn from(value: Enum7) -> Self {
        (&value).into()
    }
}
impl From<Enum7> for i8 {
    fn from(value: Enum7) -> Self {
        u8::from(value) as Self
    }
}
impl From<Enum7> for i16 {
    fn from(value: Enum7) -> Self {
        u8::from(value) as Self
    }
}
impl From<Enum7> for i32 {
    fn from(value: Enum7) -> Self {
        u8::from(value) as Self
    }
}
impl From<Enum7> for i64 {
    fn from(value: Enum7) -> Self {
        u8::from(value) as Self
    }
}
impl From<Enum7> for u16 {
    fn from(value: Enum7) -> Self {
        u8::from(value) as Self
    }
}
impl From<Enum7> for u32 {
    fn from(value: Enum7) -> Self {
        u8::from(value) as Self
    }
}
impl From<Enum7> for u64 {
    fn from(value: Enum7) -> Self {
        u8::from(value) as Self
    }
}
#[derive(Debug, Clone, PartialEq, Eq)]
#[cfg_attr(feature = "serde", derive(serde::Serialize, serde::Deserialize))]
pub struct FooData {
    b: u64,
}
#[derive(Debug, Clone, PartialEq, Eq)]
#[cfg_attr(feature = "serde", derive(serde::Serialize, serde::Deserialize))]
pub struct Foo {
    #[cfg_attr(feature = "serde", serde(flatten))]
    foo: Arc<FooData>,
}
#[derive(Debug)]
#[cfg_attr(feature = "serde", derive(serde::Serialize, serde::Deserialize))]
pub struct FooBuilder {
    pub b: u64,
}
impl FooData {
    fn conforms(bytes: &[u8]) -> bool {
        bytes.len() >= 8
    }
    fn parse(bytes: &[u8]) -> Result<Self> {
        let mut cell = Cell::new(bytes);
        let packet = Self::parse_inner(&mut cell)?;
        Ok(packet)
    }
    fn parse_inner(mut bytes: &mut Cell<&[u8]>) -> Result<Self> {
        if bytes.get().remaining() < 8 {
            return Err(Error::InvalidLengthError {
                obj: "Foo".to_string(),
                wanted: 8,
                got: bytes.get().remaining(),
            });
        }
        let chunk = bytes.get_mut().get_u64_le();
        if (chunk & 0x7f) as u8 != u8::from(Enum7::A) {
            return Err(Error::InvalidFixedValue {
                expected: u8::from(Enum7::A) as u64,
                actual: (chunk & 0x7f) as u8 as u64,
            });
        }
        let b = ((chunk >> 7) & 0x1ff_ffff_ffff_ffff_u64);
        Ok(Self { b })
    }
    fn write_to(&self, buffer: &mut BytesMut) {
        if self.b > 0x1ff_ffff_ffff_ffff_u64 {
            panic!(
                "Invalid value for {}::{}: {} > {}", "Foo", "b", self.b,
                0x1ff_ffff_ffff_ffff_u64
            );
        }
        let value = (u8::from(Enum7::A) as u64) | (self.b << 7);
        buffer.put_u64_le(value);
    }
    fn get_total_size(&self) -> usize {
        self.get_size()
    }
    fn get_size(&self) -> usize {
        8
    }
}
impl Packet for Foo {
    fn to_bytes(self) -> Bytes {
        let mut buffer = BytesMut::with_capacity(self.foo.get_size());
        self.foo.write_to(&mut buffer);
        buffer.freeze()
    }
    fn to_vec(self) -> Vec<u8> {
        self.to_bytes().to_vec()
    }
}
impl From<Foo> for Bytes {
    fn from(packet: Foo) -> Self {
        packet.to_bytes()
    }
}
impl From<Foo> for Vec<u8> {
    fn from(packet: Foo) -> Self {
        packet.to_vec()
    }
}
impl Foo {
    pub fn parse(bytes: &[u8]) -> Result<Self> {
        let mut cell = Cell::new(bytes);
        let packet = Self::parse_inner(&mut cell)?;
        Ok(packet)
    }
    fn parse_inner(mut bytes: &mut Cell<&[u8]>) -> Result<Self> {
        let data = FooData::parse_inner(&mut bytes)?;
        Self::new(Arc::new(data))
    }
    fn new(foo: Arc<FooData>) -> Result<Self> {
        Ok(Self { foo })
    }
    pub fn get_b(&self) -> u64 {
        self.foo.as_ref().b
    }
    fn write_to(&self, buffer: &mut BytesMut) {
        self.foo.write_to(buffer)
    }
    pub fn get_size(&self) -> usize {
        self.foo.get_size()
    }
}
impl FooBuilder {
    pub fn build(self) -> Foo {
        let foo = Arc::new(FooData { b: self.b });
        Foo::new(foo).unwrap()
    }
}
impl From<FooBuilder> for Foo {
    fn from(builder: FooBuilder) -> Foo {
        builder.build().into()
    }
}
